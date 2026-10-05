#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "natemu.h"

#define MAX_MAPPINGS 16
#define QUEUE_SIZE 1024
#define MAX_DATAGRAM 1500

typedef struct {
    bool used;
    struct sockaddr_in inside; // The client's own address
    int fd;                    // External socket: its port is the public port
    uint16_t port;
    uint64_t last_out_us;
} mapping_t;

typedef struct {
    bool up;
    uint64_t release_us;
    struct sockaddr_in inside; // Up: the sender. Down: the receiver
    size_t len;
    uint8_t data[MAX_DATAGRAM];
} packet_t;

struct natemu {
    natemu_config_t cfg;
    int inside_fd;
    struct sockaddr_in server;
    bool link_up;
    uint32_t rng;
    mapping_t mappings[MAX_MAPPINGS];
    packet_t queue[QUEUE_SIZE]; // Packets on the link, unordered
    size_t queued;
    natemu_stats_t stats;
};

static uint64_t now_us(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

// xorshift32 starts with tiny numbers from a small seed (seed 42 would always drop the first packet
// at 1 % loss), so the seed is scrambled first
static uint32_t scramble_seed(uint32_t x) {
    x += 0x9E3779B9u;
    x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
    x = (x ^ (x >> 13)) * 0xC2B2AE35u;
    x ^= x >> 16;

    return x ? x : 1; // xorshift never leaves 0
}

// xorshift32: fast, and reproducible from the seed
static double random_unit(natemu_t *n) {
    uint32_t x = n->rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    n->rng = x;

    return x / 4294967296.0;
}

static bool same_address(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    return a->sin_addr.s_addr == b->sin_addr.s_addr && a->sin_port == b->sin_port;
}

static int open_socket(uint32_t ip, uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);

    if (fd < 0) {
        return -1;
    }

    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(ip);

    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 || bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

// Reads one datagram. ICMP errors for earlier sends are skipped, they only mean a peer port is closed.
static bool receive(int fd, uint8_t *buf, struct sockaddr_in *from, size_t *len) {
    while (true) {
        socklen_t from_len = sizeof(*from);
        ssize_t n = recvfrom(fd, buf, MAX_DATAGRAM, 0, (struct sockaddr*)from, &from_len);

        if (n >= 0) {
            *len = (size_t)n;
            return true;
        }

        if (errno != EINTR && errno != ECONNRESET && errno != ECONNREFUSED) {
            return false;
        }
    }
}

static void drop_mapping(natemu_t *n, mapping_t *m, const char *why) {
    if (n->cfg.verbose) {
        printf("[NAT] Mapping %s:%u -> :%u %s\n", inet_ntoa(m->inside.sin_addr), ntohs(m->inside.sin_port), m->port, why);
    }

    close(m->fd);
    m->used = false;
}

static mapping_t *find_mapping(natemu_t *n, const struct sockaddr_in *inside) {
    for (int i = 0; i < MAX_MAPPINGS; i++) {
        if (n->mappings[i].used && same_address(&n->mappings[i].inside, inside)) {
            return &n->mappings[i];
        }
    }

    return NULL;
}

static mapping_t *create_mapping(natemu_t *n, const struct sockaddr_in *inside, uint64_t now) {
    for (int i = 0; i < MAX_MAPPINGS; i++) {
        mapping_t *m = &n->mappings[i];

        if (m->used) {
            continue;
        }

        int fd = open_socket(INADDR_ANY, 0); // The OS picks a fresh port, like a CGNAT does
        struct sockaddr_in addr;
        socklen_t addr_len = sizeof(addr);

        if (fd < 0 || getsockname(fd, (struct sockaddr*)&addr, &addr_len) < 0) {
            if (fd >= 0) {
                close(fd);
            }
            return NULL;
        }

        m->used = true;
        m->inside = *inside;
        m->fd = fd;
        m->port = ntohs(addr.sin_port);
        m->last_out_us = now;
        n->stats.mappings++;

        if (n->cfg.verbose) {
            printf("[NAT] Mapping %s:%u -> :%u created\n", inet_ntoa(inside->sin_addr), ntohs(inside->sin_port), m->port);
        }

        return m;
    }

    return NULL;
}

// Puts a packet on the lossy link
static void enqueue(natemu_t *n, bool up, const struct sockaddr_in *inside, const uint8_t *data, size_t len, uint64_t now) {
    const natemu_link_t *link = up ? &n->cfg.up : &n->cfg.down;
    uint64_t *lost = up ? &n->stats.up_lost : &n->stats.down_lost;

    if (random_unit(n) < link->loss) {
        (*lost)++;
        return;
    }

    int copies = random_unit(n) < link->duplicate ? 2 : 1;

    for (int i = 0; i < copies; i++) {
        if (n->queued == QUEUE_SIZE) {
            (*lost)++; // The link is saturated
            return;
        }

        double delay_ms = link->delay_ms + (random_unit(n) * 2 - 1) * link->jitter_ms;
        packet_t *p = &n->queue[n->queued++];

        p->up = up;
        p->release_us = now + (uint64_t)(delay_ms > 0 ? delay_ms * 1000 : 0);
        p->inside = *inside;
        p->len = len;
        memcpy(p->data, data, len);
    }
}

// Takes a packet off the link: uplink ones are translated here, downlink ones were already
static void deliver(natemu_t *n, const packet_t *p, uint64_t now) {
    if (!p->up) {
        sendto(n->inside_fd, p->data, p->len, 0, (const struct sockaddr*)&p->inside, sizeof(p->inside));
        n->stats.down_forwarded++;
        return;
    }

    mapping_t *m = find_mapping(n, &p->inside);

    if (!m) {
        m = create_mapping(n, &p->inside, now);
    }

    if (!m) {
        n->stats.up_lost++; // Out of mappings or sockets
        return;
    }

    m->last_out_us = now; // Only outbound traffic refreshes a mapping
    sendto(m->fd, p->data, p->len, 0, (struct sockaddr*)&n->server, sizeof(n->server));
    n->stats.up_forwarded++;
}

void natemu_config_default(natemu_config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->inside_port = 5556;
    cfg->server_ip = "127.0.0.1";
    cfg->server_port = 5555;
    cfg->mapping_timeout_ms = 30000;
    cfg->seed = 1;
}

natemu_t *natemu_new(const natemu_config_t *cfg) {
    natemu_t *n = calloc(1, sizeof(*n));

    if (!n) {
        return NULL;
    }

    n->cfg = *cfg;
    n->link_up = true;
    n->rng = scramble_seed(cfg->seed);
    n->server.sin_family = AF_INET;
    n->server.sin_port = htons(cfg->server_port);

    if (inet_pton(AF_INET, cfg->server_ip, &n->server.sin_addr) != 1) {
        free(n);
        errno = EINVAL;
        return NULL;
    }

    n->inside_fd = open_socket(INADDR_LOOPBACK, cfg->inside_port);

    if (n->inside_fd < 0) {
        int saved_errno = errno;

        free(n);
        errno = saved_errno;
        return NULL;
    }

    return n;
}

void natemu_free(natemu_t *n) {
    if (!n) {
        return;
    }

    for (int i = 0; i < MAX_MAPPINGS; i++) {
        if (n->mappings[i].used) {
            close(n->mappings[i].fd);
        }
    }

    close(n->inside_fd);
    free(n);
}

void natemu_service(natemu_t *n) {
    uint64_t now = now_us();
    uint8_t buf[MAX_DATAGRAM];
    struct sockaddr_in from;
    size_t len;

    // Idle mappings expire first, so nothing gets translated through a dead one
    for (int i = 0; i < MAX_MAPPINGS; i++) {
        mapping_t *m = &n->mappings[i];

        if (m->used && now - m->last_out_us >= (uint64_t)n->cfg.mapping_timeout_ms * 1000) {
            drop_mapping(n, m, "expired");
            n->stats.expired++;
        }
    }

    // Client -> link
    while (receive(n->inside_fd, buf, &from, &len)) {
        if (!n->link_up) {
            n->stats.link_dropped++;
            continue;
        }

        enqueue(n, true, &from, buf, len, now);
    }

    // Server -> NAT -> link
    for (int i = 0; i < MAX_MAPPINGS; i++) {
        mapping_t *m = &n->mappings[i];

        while (m->used && receive(m->fd, buf, &from, &len)) {
            // Address-dependent filtering: a mapping only accepts the address it has sent to
            if (!same_address(&from, &n->server)) {
                n->stats.down_filtered++;
                continue;
            }

            if (!n->link_up) {
                n->stats.link_dropped++;
                continue;
            }

            enqueue(n, false, &m->inside, buf, len, now);
        }
    }

    // Due packets leave the link earliest first, which is where jitter reorders them
    while (true) {
        size_t next = n->queued;

        for (size_t i = 0; i < n->queued; i++) {
            if (n->queue[i].release_us <= now && (next == n->queued || n->queue[i].release_us < n->queue[next].release_us)) {
                next = i;
            }
        }

        if (next == n->queued) {
            break;
        }

        deliver(n, &n->queue[next], now);
        n->queue[next] = n->queue[--n->queued];
    }
}

void natemu_rebind(natemu_t *n) {
    for (int i = 0; i < MAX_MAPPINGS; i++) {
        if (n->mappings[i].used) {
            drop_mapping(n, &n->mappings[i], "dropped by rebind");
        }
    }

    n->stats.rebinds++;
}

void natemu_set_link(natemu_t *n, bool up) {
    if (!up) {
        n->stats.link_dropped += n->queued;
        n->queued = 0;
    }

    if (n->cfg.verbose && up != n->link_up) {
        printf("[NAT] Link %s\n", up ? "up" : "down");
    }

    n->link_up = up;
}

uint16_t natemu_public_port(const natemu_t *n) {
    for (int i = 0; i < MAX_MAPPINGS; i++) {
        if (n->mappings[i].used) {
            return n->mappings[i].port;
        }
    }

    return 0;
}

natemu_stats_t natemu_stats(const natemu_t *n) {
    return n->stats;
}
