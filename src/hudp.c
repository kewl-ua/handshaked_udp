#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "hudp.h"
#include "protocol.h"

struct hudp {
    int fd;
    bool is_server;
    hudp_state_t state;
    hudp_config_t cfg;
    uint8_t session_id;       // 0 while a server has no session
    struct sockaddr_in peer;  // Client: the server. Server: the client's latest address
    bool has_peer;
    uint64_t last_rx_ms;      // Last packet of the session from the peer
    uint64_t last_tx_ms;      // Last packet sent to the peer
    uint64_t last_req_ms;     // Last CONN_REQ, 0 means "send one now"
    bool has_pending;         // A second event produced by the same datagram
    hudp_event_t pending;
    uint8_t rx[HUDP_HEADER_SIZE + HUDP_MAX_PAYLOAD + 1]; // +1 byte detects oversized datagrams
    uint8_t tx[HUDP_HEADER_SIZE + HUDP_MAX_PAYLOAD];
};

typedef enum {
    RX_EMPTY,   // Nothing left to read
    RX_SKIPPED, // A datagram was consumed without producing an event
    RX_EVENT,
    RX_ERROR,
} rx_result_t;

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static uint8_t random_session_id(uint8_t avoid) {
    uint8_t byte;
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd < 0 || read(fd, &byte, 1) != 1) {
        byte = (uint8_t)(now_ms() ^ (uint64_t)getpid());
    }

    if (fd >= 0) {
        close(fd);
    }

    uint8_t id = 1 + byte % 255; // Session ID [1; 255], 0 means "no session"

    return id == avoid ? id % 255 + 1 : id;
}

static bool would_block(int err) {
#if EWOULDBLOCK != EAGAIN
    if (err == EWOULDBLOCK) {
        return true;
    }
#endif
    return err == EAGAIN;
}

static bool same_address(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static int send_packet(hudp_t *h, uint8_t type, const void *payload, size_t len) {
    h->tx[0] = type;
    h->tx[1] = h->session_id;

    if (len > 0) {
        memcpy(h->tx + HUDP_HEADER_SIZE, payload, len);
    }

    ssize_t sent = h->is_server
        ? sendto(h->fd, h->tx, HUDP_HEADER_SIZE + len, 0, (struct sockaddr*)&h->peer, sizeof(h->peer))
        : send(h->fd, h->tx, HUDP_HEADER_SIZE + len, 0);

    h->last_tx_ms = now_ms();

    return sent < 0 ? -1 : 0;
}

static rx_result_t client_receive(hudp_t *h, hudp_event_t *ev, uint8_t type, uint8_t session, size_t len, uint64_t now) {
    if (session != h->session_id) {
        return RX_SKIPPED;
    }

    if (h->state == HUDP_STATE_CONNECTING) {
        if (type != MSG_CONN_ACK) {
            return RX_SKIPPED;
        }

        h->state = HUDP_STATE_CONNECTED;
        h->last_rx_ms = now;
        ev->type = HUDP_EVENT_CONNECTED;

        return RX_EVENT;
    }

    // Any packet of the session proves the link is alive, a late duplicate CONN_ACK too
    h->last_rx_ms = now;

    if (type != MSG_DATA || len == HUDP_HEADER_SIZE) {
        return RX_SKIPPED; // Duplicate CONN_ACK or keep-alive
    }

    ev->type = HUDP_EVENT_DATA;
    ev->data = h->rx + HUDP_HEADER_SIZE;
    ev->len = len - HUDP_HEADER_SIZE;

    return RX_EVENT;
}

static rx_result_t server_receive(hudp_t *h, hudp_event_t *ev, uint8_t type, uint8_t session, size_t len,
                                  const struct sockaddr_in *from, uint64_t now) {
    bool moved = h->has_peer && !same_address(from, &h->peer);

    if (type == MSG_CONN_REQ) {
        // A live session is never handed to another CONN_REQ: a new client waits until the old one times out
        if (h->state == HUDP_STATE_CONNECTED && session != h->session_id) {
            return RX_SKIPPED;
        }

        bool fresh = h->state != HUDP_STATE_CONNECTED;

        h->state = HUDP_STATE_CONNECTED;
        h->session_id = session;
        h->peer = *from;
        h->has_peer = true;
        h->last_rx_ms = now;

        // Also answers a repeated CONN_REQ whose CONN_ACK got lost
        send_packet(h, MSG_CONN_ACK, NULL, 0);

        if (fresh) {
            ev->type = HUDP_EVENT_CONNECTED;
        } else if (moved) {
            ev->type = HUDP_EVENT_MIGRATED;
        } else {
            return RX_SKIPPED;
        }

        return RX_EVENT;
    }

    if (type != MSG_DATA || h->state != HUDP_STATE_CONNECTED || session != h->session_id) {
        return RX_SKIPPED;
    }

    h->last_rx_ms = now;

    hudp_event_t data_ev = {
        .type = HUDP_EVENT_DATA,
        .session_id = session,
        .data = h->rx + HUDP_HEADER_SIZE,
        .len = len - HUDP_HEADER_SIZE,
    };

    if (moved) {
        // The NAT gave the client a new external port: from now on replies go there
        h->peer = *from;
        ev->type = HUDP_EVENT_MIGRATED;

        if (data_ev.len > 0) {
            h->pending = data_ev;
            h->has_pending = true;
        }

        return RX_EVENT;
    }

    if (data_ev.len == 0) {
        return RX_SKIPPED; // Keep-alive
    }

    *ev = data_ev;

    return RX_EVENT;
}

static rx_result_t receive_one(hudp_t *h, hudp_event_t *ev) {
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    ssize_t n = recvfrom(h->fd, h->rx, sizeof(h->rx), 0, (struct sockaddr*)&from, &from_len);

    if (n < 0) {
        if (would_block(errno)) {
            return RX_EMPTY;
        }

        // ICMP errors for earlier sends (peer port closed) surface here and are not fatal
        if (errno == EINTR || errno == ECONNREFUSED || errno == ECONNRESET) {
            return RX_SKIPPED;
        }

        return RX_ERROR;
    }

    // Too short even for the header, or too long for any valid packet
    if (n < HUDP_HEADER_SIZE || n > HUDP_HEADER_SIZE + HUDP_MAX_PAYLOAD) {
        return RX_SKIPPED;
    }

    uint8_t type = h->rx[0];
    uint8_t session = h->rx[1];

    memset(ev, 0, sizeof(*ev));
    ev->session_id = session;

    return h->is_server
        ? server_receive(h, ev, type, session, (size_t)n, &from, now_ms())
        : client_receive(h, ev, type, session, (size_t)n, now_ms());
}

// Sends what is due and reports link loss. Returns true when ev holds HUDP_EVENT_LOST.
static bool run_timers(hudp_t *h, uint64_t now, hudp_event_t *ev) {
    if (h->state == HUDP_STATE_CONNECTING) {
        if (h->last_req_ms == 0 || now - h->last_req_ms >= h->cfg.handshake_interval_ms) {
            send_packet(h, MSG_CONN_REQ, NULL, 0);
            h->last_req_ms = now;
        }

        return false;
    }

    if (h->state != HUDP_STATE_CONNECTED) {
        return false;
    }

    if (now - h->last_rx_ms >= h->cfg.link_timeout_ms) {
        memset(ev, 0, sizeof(*ev));
        ev->type = HUDP_EVENT_LOST;
        ev->session_id = h->session_id;

        if (h->is_server) {
            h->state = HUDP_STATE_LISTENING;
            h->session_id = 0;
            h->has_peer = false;
        } else {
            // Knock again right away, with a new Session ID so stale packets of the old one are ignored
            h->state = HUDP_STATE_CONNECTING;
            h->session_id = random_session_id(h->session_id);
            h->last_req_ms = 0;
        }

        return true;
    }

    // Keeps the NAT mapping open and the peer's link timer fed when the application has nothing to send
    if (now - h->last_tx_ms >= h->cfg.keepalive_interval_ms) {
        send_packet(h, MSG_DATA, NULL, 0);
    }

    return false;
}

static uint64_t next_deadline(const hudp_t *h) {
    if (h->state == HUDP_STATE_CONNECTING) {
        return h->last_req_ms + h->cfg.handshake_interval_ms;
    }

    if (h->state == HUDP_STATE_CONNECTED) {
        uint64_t lost = h->last_rx_ms + h->cfg.link_timeout_ms;
        uint64_t keepalive = h->last_tx_ms + h->cfg.keepalive_interval_ms;

        return lost < keepalive ? lost : keepalive;
    }

    return UINT64_MAX;
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);

    return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static hudp_t *endpoint_new(bool is_server, const hudp_config_t *cfg) {
    hudp_t *h = calloc(1, sizeof(*h));

    if (!h) {
        return NULL;
    }

    if (cfg) {
        h->cfg = *cfg;
    } else {
        hudp_config_default(&h->cfg);
    }

    h->is_server = is_server;
    h->fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (h->fd < 0 || set_nonblocking(h->fd) < 0) {
        hudp_free(h);
        return NULL;
    }

    return h;
}

void hudp_config_default(hudp_config_t *cfg) {
    cfg->handshake_interval_ms = 250;
    cfg->keepalive_interval_ms = 100;
    cfg->link_timeout_ms = 1000;
}

hudp_t *hudp_client_new(const char *server_ip, uint16_t port, const hudp_config_t *cfg) {
    hudp_t *h = endpoint_new(false, cfg);

    if (!h) {
        return NULL;
    }

    h->peer.sin_family = AF_INET;
    h->peer.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &h->peer.sin_addr) != 1) {
        hudp_free(h);
        errno = EINVAL;
        return NULL;
    }

    // connect() makes the kernel drop datagrams from anyone but the server
    if (connect(h->fd, (struct sockaddr*)&h->peer, sizeof(h->peer)) < 0) {
        hudp_free(h);
        return NULL;
    }

    h->has_peer = true;
    h->state = HUDP_STATE_CONNECTING;
    h->session_id = random_session_id(0);

    return h;
}

hudp_t *hudp_server_new(uint16_t port, const hudp_config_t *cfg) {
    hudp_t *h = endpoint_new(true, cfg);

    if (!h) {
        return NULL;
    }

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(h->fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        hudp_free(h);
        return NULL;
    }

    h->state = HUDP_STATE_LISTENING;

    return h;
}

void hudp_free(hudp_t *h) {
    if (!h) {
        return;
    }

    int saved_errno = errno; // Keeps the error of a failed hudp_*_new() for the caller

    if (h->fd >= 0) {
        close(h->fd);
    }

    free(h);
    errno = saved_errno;
}

int hudp_service(hudp_t *h, hudp_event_t *ev, int timeout_ms) {
    uint64_t deadline = timeout_ms < 0 ? UINT64_MAX : now_ms() + (uint64_t)timeout_ms;

    while (true) {
        if (h->has_pending) {
            *ev = h->pending;
            h->has_pending = false;
            return 1;
        }

        rx_result_t rx = receive_one(h, ev);

        if (rx == RX_EVENT) {
            return 1;
        }

        if (rx == RX_ERROR) {
            return -1;
        }

        if (rx == RX_SKIPPED) {
            continue;
        }

        // The socket is drained: now handle the timers
        uint64_t now = now_ms();

        if (run_timers(h, now, ev)) {
            return 1;
        }

        if (now >= deadline) {
            return 0;
        }

        uint64_t wake = next_deadline(h);

        if (wake > deadline) {
            wake = deadline;
        }

        int wait_ms = -1;

        if (wake != UINT64_MAX) {
            wait_ms = wake <= now ? 0 : (wake - now > INT_MAX ? INT_MAX : (int)(wake - now));
        }

        struct pollfd pfd = { .fd = h->fd, .events = POLLIN };

        if (poll(&pfd, 1, wait_ms) < 0 && errno != EINTR) {
            return -1;
        }
    }
}

int hudp_send(hudp_t *h, const void *payload, size_t len) {
    if (h->state != HUDP_STATE_CONNECTED) {
        errno = ENOTCONN;
        return -1;
    }

    if (len > HUDP_MAX_PAYLOAD) {
        errno = EMSGSIZE;
        return -1;
    }

    return send_packet(h, MSG_DATA, payload, len);
}

hudp_state_t hudp_state(const hudp_t *h) {
    return h->state;
}

int hudp_peer(const hudp_t *h, char *buf, size_t cap) {
    char ip[INET_ADDRSTRLEN];

    if (!h->has_peer || !inet_ntop(AF_INET, &h->peer.sin_addr, ip, sizeof(ip))) {
        return -1;
    }

    snprintf(buf, cap, "%s:%u", ip, (unsigned)ntohs(h->peer.sin_port));

    return 0;
}

int hudp_fd(const hudp_t *h) {
    return h->fd;
}
