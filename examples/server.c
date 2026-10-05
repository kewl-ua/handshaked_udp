#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "hudp.h"

// Usage: server [port]
int main(int argc, char *argv[]) {
    setvbuf(stdout, NULL, _IOLBF, 0); // Every log line shows up at once, also when written to a file

    uint16_t port = argc > 1 ? (uint16_t)atoi(argv[1]) : HUDP_DEFAULT_PORT;
    hudp_t *h = hudp_server_new(port, NULL);

    if (!h) {
        perror("Server creation failed");
        return 1;
    }

    printf("[SERVER] Running on port %u. Waiting for client...\n", port);

    uint8_t payload[26] = {0};
    char peer[32];

    while (true) {
        hudp_event_t ev;

        // Sleeps until something happens; keep-alives and link timeouts run inside
        int res = hudp_service(h, &ev, -1);

        if (res < 0) {
            perror("Service failed");
            break;
        }

        if (res == 0) {
            continue;
        }

        switch (ev.type) {
        case HUDP_EVENT_CONNECTED:
            hudp_peer(h, peer, sizeof(peer));
            printf("[SERVER] Client %s connected. Session %d\n", peer, ev.session_id);
            break;
        case HUDP_EVENT_MIGRATED:
            hudp_peer(h, peer, sizeof(peer));
            printf("[SERVER] Client moved to %s. Session %d\n", peer, ev.session_id);
            break;
        case HUDP_EVENT_DATA:
            // ev.data / ev.len hold the payload from the client
            // Fill the payload with the answer
            // ...

            // Replies always go to the client's latest address
            hudp_send(h, payload, sizeof(payload));
            break;
        case HUDP_EVENT_LOST:
            printf("[SERVER] Client lost. Waiting for a new one...\n");
            break;
        default:
            break;
        }
    }

    hudp_free(h);

    return 0;
}
