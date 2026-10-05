#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

#include "hudp.h"

#define SERVER_IP "10.255.0.2" // Default server public IP, overridden by argv[1]
#define TICK_NS 4000000L // 4 ms cycle step for matching 250 Hz rate

// Usage: client [server_ip] [port]
int main(int argc, char *argv[]) {
    const char *server_ip = argc > 1 ? argv[1] : SERVER_IP;
    uint16_t port = argc > 2 ? (uint16_t)atoi(argv[2]) : HUDP_DEFAULT_PORT;
    hudp_t *h = hudp_client_new(server_ip, port, NULL);

    if (!h) {
        perror("Client creation failed");
        return 1;
    }

    printf("[CLIENT] Knocking the server %s:%u...\n", server_ip, port);

    uint8_t payload[26] = {0};
    struct timespec next_tick;

    clock_gettime(CLOCK_MONOTONIC, &next_tick);

    while (true) {
        hudp_event_t ev;
        int res;

        // Handle everything that arrived since the last tick
        while ((res = hudp_service(h, &ev, 0)) > 0) {
            switch (ev.type) {
            case HUDP_EVENT_CONNECTED:
                printf("[CLIENT] Server responded! Connection established. Session %d\n", ev.session_id);
                break;
            case HUDP_EVENT_DATA:
                // ev.data / ev.len hold the payload from the server
                // ...
                break;
            case HUDP_EVENT_LOST:
                printf("[CLIENT] Server lost, knocking again...\n");
                break;
            default:
                break;
            }
        }

        if (res < 0) {
            perror("Service failed");
            break;
        }

        if (hudp_state(h) == HUDP_STATE_CONNECTED) {
            // Filling payload with fresh data
            // ...
            hudp_send(h, payload, sizeof(payload));
        }

        // Absolute deadlines keep the rate at 250 Hz however long the work above took
        next_tick.tv_nsec += TICK_NS;

        if (next_tick.tv_nsec >= 1000000000L) {
            next_tick.tv_nsec -= 1000000000L;
            next_tick.tv_sec += 1;
        }

        clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next_tick, NULL);
    }

    hudp_free(h);

    return 0;
}
