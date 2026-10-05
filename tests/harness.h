// Shared helpers for the C tests: a client and a server endpoint that record their events,
// CHECK, and WAIT_FOR, which keeps servicing everything until a condition holds.
// Each test file defines step(): one round of servicing everything it has.
#ifndef HARNESS_H
#define HARNESS_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hudp.h"

typedef struct {
    hudp_t *h;
    int events[HUDP_EVENT_LOST + 1];
    uint8_t session_id; // Of the latest event
    char data[64];      // Latest payload, as a string
} endpoint_t;

static endpoint_t client, server;
static int failures;

#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond);     \
            failures++;                                                           \
            return;                                                               \
        }                                                                         \
    } while (0)

static inline uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static inline hudp_config_t fast_config(void) {
    hudp_config_t cfg;

    hudp_config_default(&cfg);
    cfg.handshake_interval_ms = 20;
    cfg.keepalive_interval_ms = 30;
    cfg.link_timeout_ms = 200;

    return cfg;
}

static inline void drain(endpoint_t *e) {
    hudp_event_t ev;

    while (e->h && hudp_service(e->h, &ev, 0) > 0) {
        e->events[ev.type]++;
        e->session_id = ev.session_id;

        if (ev.type == HUDP_EVENT_DATA) {
            size_t n = ev.len < sizeof(e->data) - 1 ? ev.len : sizeof(e->data) - 1;

            memcpy(e->data, ev.data, n);
            e->data[n] = '\0';
        }
    }
}

// One round of servicing, defined by each test file
static void step(void);

// Services everything until cond holds or ms run out
#define WAIT_FOR(cond, ms)                                     \
    do {                                                       \
        uint64_t until_ = now_ms() + (ms);                     \
        while (!(cond) && now_ms() < until_) {                 \
            step();                                            \
        }                                                      \
    } while (0)

#define PUMP(ms) WAIT_FOR(false, ms)

// Runs one test on fresh endpoints and frees them afterwards; cleanup may free more.
static inline void run(const char *name, void (*test)(void), void (*cleanup)(void)) {
    int before = failures;

    memset(&client, 0, sizeof(client));
    memset(&server, 0, sizeof(server));

    test();

    hudp_free(client.h);
    hudp_free(server.h);

    if (cleanup) {
        cleanup();
    }

    printf("%s %s\n", failures == before ? "PASS" : "FAIL", name);
}

static inline int report(void) {
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "OK", failures);

    return failures ? 1 : 0;
}

#endif
