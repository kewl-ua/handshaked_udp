// Benchmark: libhudp against plain UDP behind the CGNAT emulator.
// Both sides stream at 250 Hz, like control one way and telemetry the other. Every packet carries its
// sequence number and send time; the receiver, in the same process and on the same clock, measures
// delivery, one-way latency and the longest gap in the stream.
// Plain UDP means what the README means: a fixed peer address, no handshake, no keep-alive.
#include <errno.h>
#include <fcntl.h>
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
#include "natemu.h"

#define SERVER_PORT 17000
#define NAT_PORT 17001
#define TICK_US 4000 // 250 Hz
#define PAYLOAD_SIZE 26
#define MAX_PACKETS 65536 // Per direction and run
#define NAT_TIMEOUT_MS 2000 // Compressed: real CGNAT mapping timeouts are tens of seconds

typedef struct {
    const char *name;
    natemu_link_t link;        // Both directions
    uint32_t rebind_every_ms;  // 0: never
    uint32_t outage_from_ms;
    uint32_t outage_ms;
    uint32_t idle_from_ms;     // The client application has nothing to send
    uint32_t idle_ms;
} scenario_t;

#define STARLINK { .delay_ms = 20, .jitter_ms = 5, .loss = 0.01 }

static const scenario_t scenarios[] = {
    { .name = "Clean link" },
    { .name = "Starlink-like: 20 ± 5 ms, 1 % loss", .link = STARLINK },
    { .name = "+ NAT port change every 2 s", .link = STARLINK, .rebind_every_ms = 2000 },
    { .name = "+ client idle for 4 s", .link = STARLINK, .idle_from_ms = 2000, .idle_ms = 4000 },
    { .name = "+ 2 s link outage", .link = STARLINK, .outage_from_ms = 3000, .outage_ms = 2000 },
};

typedef struct {
    uint32_t sent;
    uint32_t received; // Unique, duplicates are not counted
    uint8_t seen[MAX_PACKETS / 8];
    uint32_t latency_us[MAX_PACKETS];
    uint64_t first_rx_us;
    uint64_t last_rx_us;
    uint64_t longest_gap_us;
    bool gap_is_trailing; // The longest gap lasted until the end: the stream never came back
} stream_t;

typedef struct {
    const char *name;
    bool (*open)(void);
    void (*close)(void);
    void (*pump)(void);
    void (*client_send)(const uint8_t *packet);
    void (*server_send)(const uint8_t *packet);
} impl_t;

static stream_t up, down; // Client -> server, server -> client
static int migrations, losses, connects;
static uint64_t run_start_us;

static uint64_t now_us(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000000 + (uint64_t)ts.tv_nsec / 1000;
}

static void make_packet(uint8_t *buf, uint32_t seq, uint64_t t_us) {
    memset(buf, 0, PAYLOAD_SIZE);
    memcpy(buf, &seq, sizeof(seq));
    memcpy(buf + sizeof(seq), &t_us, sizeof(t_us));
}

static void on_packet(stream_t *s, const uint8_t *data, size_t len) {
    uint64_t now = now_us();
    uint32_t seq;
    uint64_t t_us;

    if (len < sizeof(seq) + sizeof(t_us)) {
        return;
    }

    memcpy(&seq, data, sizeof(seq));
    memcpy(&t_us, data + sizeof(seq), sizeof(t_us));

    if (seq >= MAX_PACKETS || (s->seen[seq / 8] & (1 << (seq % 8)))) {
        return;
    }

    s->seen[seq / 8] |= (uint8_t)(1 << (seq % 8));
    s->latency_us[s->received++] = (uint32_t)(now - t_us);

    if (s->last_rx_us && now - s->last_rx_us > s->longest_gap_us) {
        s->longest_gap_us = now - s->last_rx_us;
    }

    if (!s->first_rx_us) {
        s->first_rx_us = now;
    }

    s->last_rx_us = now;
}

static void finish(stream_t *s, uint64_t end_us) {
    if (s->last_rx_us && end_us - s->last_rx_us > s->longest_gap_us) {
        s->longest_gap_us = end_us - s->last_rx_us;
        s->gap_is_trailing = true;
    }
}

// --- libhudp ---------------------------------------------------------------

static hudp_t *hudp_client, *hudp_server;

static bool hudp_open(void) {
    hudp_server = hudp_server_new(SERVER_PORT, NULL);
    hudp_client = hudp_client_new("127.0.0.1", NAT_PORT, NULL);

    return hudp_server && hudp_client;
}

static void hudp_close(void) {
    hudp_free(hudp_client);
    hudp_free(hudp_server);
}

static void hudp_pump(void) {
    hudp_event_t ev;

    while (hudp_service(hudp_client, &ev, 0) > 0) {
        if (ev.type == HUDP_EVENT_DATA) {
            on_packet(&down, ev.data, ev.len);
        } else if (ev.type == HUDP_EVENT_CONNECTED) {
            connects++;
        } else if (ev.type == HUDP_EVENT_LOST) {
            losses++;
        }
    }

    while (hudp_service(hudp_server, &ev, 0) > 0) {
        if (ev.type == HUDP_EVENT_DATA) {
            on_packet(&up, ev.data, ev.len);
        } else if (ev.type == HUDP_EVENT_MIGRATED) {
            migrations++;
        }
    }
}

static void hudp_client_send(const uint8_t *packet) {
    hudp_send(hudp_client, packet, PAYLOAD_SIZE); // Fails while (re)connecting: that tick is lost
}

static void hudp_server_send(const uint8_t *packet) {
    hudp_send(hudp_server, packet, PAYLOAD_SIZE);
}

// --- plain UDP -------------------------------------------------------------

static int plain_client = -1, plain_server = -1;
static struct sockaddr_in pinned; // The first client address the server saw
static bool has_pinned;

static int udp_socket(uint16_t port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (fd < 0) {
        return -1;
    }

    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);

    return fd;
}

// Reads one datagram, skipping ICMP errors for earlier sends. Returns its length or -1.
static ssize_t udp_receive(int fd, uint8_t *buf, size_t cap, struct sockaddr_in *from) {
    while (true) {
        socklen_t from_len = sizeof(*from);
        ssize_t n = recvfrom(fd, buf, cap, 0, (struct sockaddr*)from, &from_len);

        if (n >= 0 || (errno != ECONNRESET && errno != ECONNREFUSED && errno != EINTR)) {
            return n;
        }
    }
}

static bool plain_open(void) {
    struct sockaddr_in nat;

    memset(&nat, 0, sizeof(nat));
    nat.sin_family = AF_INET;
    nat.sin_port = htons(NAT_PORT);
    nat.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    has_pinned = false;
    plain_server = udp_socket(SERVER_PORT);
    plain_client = udp_socket(0);

    return plain_server >= 0 && plain_client >= 0 && connect(plain_client, (struct sockaddr*)&nat, sizeof(nat)) == 0;
}

static void plain_close(void) {
    close(plain_client);
    close(plain_server);
}

static void plain_pump(void) {
    uint8_t buf[64];
    struct sockaddr_in from;
    ssize_t n;

    while ((n = udp_receive(plain_client, buf, sizeof(buf), &from)) >= 0) {
        on_packet(&down, buf, (size_t)n);
    }

    while ((n = udp_receive(plain_server, buf, sizeof(buf), &from)) >= 0) {
        if (!has_pinned) {
            pinned = from;
            has_pinned = true;
        }

        on_packet(&up, buf, (size_t)n);
    }
}

static void plain_client_send(const uint8_t *packet) {
    send(plain_client, packet, PAYLOAD_SIZE, 0);
}

static void plain_server_send(const uint8_t *packet) {
    if (has_pinned) {
        sendto(plain_server, packet, PAYLOAD_SIZE, 0, (struct sockaddr*)&pinned, sizeof(pinned));
    }
}

static const impl_t impls[] = {
    { "hudp", hudp_open, hudp_close, hudp_pump, hudp_client_send, hudp_server_send },
    { "plain UDP", plain_open, plain_close, plain_pump, plain_client_send, plain_server_send },
};

// --- runner ----------------------------------------------------------------

static int compare_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t*)a, y = *(const uint32_t*)b;

    return (x > y) - (x < y);
}

static double percentile_ms(stream_t *s, double p) {
    if (s->received == 0) {
        return 0;
    }

    qsort(s->latency_us, s->received, sizeof(uint32_t), compare_u32);

    return s->latency_us[(size_t)(p * (s->received - 1))] / 1000.0;
}

static bool run_one(const scenario_t *sc, const impl_t *im, uint32_t run_ms) {
    natemu_config_t cfg;
    uint8_t packet[PAYLOAD_SIZE];

    memset(&up, 0, sizeof(up));
    memset(&down, 0, sizeof(down));
    migrations = losses = connects = 0;

    natemu_config_default(&cfg);
    cfg.inside_port = NAT_PORT;
    cfg.server_port = SERVER_PORT;
    cfg.mapping_timeout_ms = NAT_TIMEOUT_MS;
    cfg.up = sc->link;
    cfg.down = sc->link;
    cfg.seed = 42;

    natemu_t *nat = natemu_new(&cfg);

    if (!nat || !im->open()) {
        perror("bench setup failed");
        return false;
    }

    uint64_t start = now_us();
    uint64_t end = start + (uint64_t)run_ms * 1000;

    run_start_us = start;
    uint64_t next_tick = start;
    uint64_t next_rebind = sc->rebind_every_ms ? start + (uint64_t)sc->rebind_every_ms * 1000 : 0;
    bool link_down = false;

    // Busy loop: sleeping would add the scheduler's granularity to every measured latency
    for (uint64_t now = start; now < end; now = now_us()) {
        uint64_t t_ms = (now - start) / 1000;

        if (sc->outage_ms) {
            bool down_now = t_ms >= sc->outage_from_ms && t_ms < sc->outage_from_ms + sc->outage_ms;

            if (down_now != link_down) {
                natemu_set_link(nat, !down_now);
                link_down = down_now;
            }
        }

        if (next_rebind && now >= next_rebind) {
            natemu_rebind(nat);
            next_rebind += (uint64_t)sc->rebind_every_ms * 1000;
        }

        if (now >= next_tick) {
            bool idle = sc->idle_ms && t_ms >= sc->idle_from_ms && t_ms < sc->idle_from_ms + sc->idle_ms;

            next_tick += TICK_US;

            if (!idle && up.sent < MAX_PACKETS) {
                make_packet(packet, up.sent++, now);
                im->client_send(packet);
            }

            if (down.sent < MAX_PACKETS) {
                make_packet(packet, down.sent++, now);
                im->server_send(packet);
            }
        }

        natemu_service(nat);
        im->pump();
    }

    finish(&up, now_us());
    finish(&down, now_us());
    im->close();
    natemu_free(nat);

    return true;
}

static void append(char *buf, size_t cap, int count, const char *what) {
    size_t len = strlen(buf);

    if (count > 0) {
        snprintf(buf + len, cap - len, "%s%d %s", len ? ", " : "", count, what);
    }
}

static void print_row(const char *scenario, const impl_t *im, bool is_hudp) {
    char first[32] = "never";
    char gap[32];
    char events[96] = "";

    if (down.first_rx_us) {
        snprintf(first, sizeof(first), "%.0f ms", (down.first_rx_us - run_start_us) / 1000.0);
    }

    if (down.received == 0 || down.gap_is_trailing) {
        snprintf(gap, sizeof(gap), "never recovered");
    } else {
        snprintf(gap, sizeof(gap), "%.0f ms", down.longest_gap_us / 1000.0);
    }

    if (is_hudp) {
        append(events, sizeof(events), migrations, "migrations");
        append(events, sizeof(events), losses, "lost");
        append(events, sizeof(events), connects - 1, "reconnects");
    }

    printf("| %s | %s | %.1f %% | %.1f %% | %.1f / %.1f ms | %s | %s | %s |\n",
           scenario, im->name,
           up.sent ? 100.0 * up.received / up.sent : 0.0,
           down.sent ? 100.0 * down.received / down.sent : 0.0,
           percentile_ms(&down, 0.50), percentile_ms(&down, 0.99),
           first, gap, events[0] ? events : "—");
}

int main(int argc, char *argv[]) {
    uint32_t run_ms = argc > 1 ? (uint32_t)(atof(argv[1]) * 1000) : 8000;
    size_t n_scenarios = sizeof(scenarios) / sizeof(scenarios[0]);
    size_t n_impls = sizeof(impls) / sizeof(impls[0]);

    printf("Each case runs %.1f s, 250 Hz both ways, %d-byte payloads, NAT mapping timeout %d ms.\n\n",
           run_ms / 1000.0, PAYLOAD_SIZE, NAT_TIMEOUT_MS);
    printf("| Scenario | Protocol | Up delivered | Down delivered | Down latency p50 / p99 | First down packet | Longest down gap | hudp events |\n");
    printf("|---|---|---|---|---|---|---|---|\n");
    fflush(stdout);

    for (size_t s = 0; s < n_scenarios; s++) {
        for (size_t i = 0; i < n_impls; i++) {
            if (!run_one(&scenarios[s], &impls[i], run_ms)) {
                return 1;
            }

            print_row(i == 0 ? scenarios[s].name : "", &impls[i], impls[i].open == hudp_open);
            fflush(stdout);
        }
    }

    return 0;
}
