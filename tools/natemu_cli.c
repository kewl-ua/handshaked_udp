// natemu: the CGNAT emulator as a standalone relay in front of any UDP server on this host.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>

#include "natemu.h"

static void usage(void) {
    printf("Usage: natemu [options]\n"
           "  --listen PORT        port clients send to (default 5556)\n"
           "  --server IP:PORT     real server (default 127.0.0.1:5555)\n"
           "  --timeout MS         idle mapping timeout (default 30000)\n"
           "  --delay MS           one-way delay, each direction (default 0)\n"
           "  --jitter MS          +- spread around the delay (default 0)\n"
           "  --loss PCT           packet loss, each direction (default 0)\n"
           "  --dup PCT            packet duplication, each direction (default 0)\n"
           "  --rebind-every MS    drop all mappings periodically, like a handover (default never)\n"
           "  --seed N             random seed (default 1)\n");
}

static uint64_t now_ms(void) {
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);

    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

int main(int argc, char *argv[]) {
    natemu_config_t cfg;
    natemu_link_t link = {0};
    uint32_t rebind_every_ms = 0;
    static char server_ip[64];

    setvbuf(stdout, NULL, _IOLBF, 0); // Every log line shows up at once, also when written to a file
    natemu_config_default(&cfg);
    cfg.verbose = true;

    for (int i = 1; i < argc; i++) {
        const char *opt = argv[i];
        const char *val = i + 1 < argc ? argv[i + 1] : NULL;

        if (strcmp(opt, "--help") == 0 || !val) {
            usage();
            return strcmp(opt, "--help") == 0 ? 0 : 1;
        }

        i++;

        if (strcmp(opt, "--listen") == 0) {
            cfg.inside_port = (uint16_t)atoi(val);
        } else if (strcmp(opt, "--server") == 0) {
            const char *colon = strrchr(val, ':');

            if (!colon || (size_t)(colon - val) >= sizeof(server_ip)) {
                usage();
                return 1;
            }

            memcpy(server_ip, val, (size_t)(colon - val));
            server_ip[colon - val] = '\0';
            cfg.server_ip = server_ip;
            cfg.server_port = (uint16_t)atoi(colon + 1);
        } else if (strcmp(opt, "--timeout") == 0) {
            cfg.mapping_timeout_ms = (uint32_t)atoi(val);
        } else if (strcmp(opt, "--delay") == 0) {
            link.delay_ms = (uint32_t)atoi(val);
        } else if (strcmp(opt, "--jitter") == 0) {
            link.jitter_ms = (uint32_t)atoi(val);
        } else if (strcmp(opt, "--loss") == 0) {
            link.loss = atof(val) / 100;
        } else if (strcmp(opt, "--dup") == 0) {
            link.duplicate = atof(val) / 100;
        } else if (strcmp(opt, "--rebind-every") == 0) {
            rebind_every_ms = (uint32_t)atoi(val);
        } else if (strcmp(opt, "--seed") == 0) {
            cfg.seed = (uint32_t)strtoul(val, NULL, 10);
        } else {
            usage();
            return 1;
        }
    }

    cfg.up = link;
    cfg.down = link;

    natemu_t *n = natemu_new(&cfg);

    if (!n) {
        perror("NAT emulator creation failed");
        return 1;
    }

    printf("[NAT] Listening on 127.0.0.1:%u, forwarding to %s:%u\n", cfg.inside_port, cfg.server_ip, cfg.server_port);

    uint64_t next_rebind = rebind_every_ms ? now_ms() + rebind_every_ms : 0;
    struct timespec pause = { 0, 200000 }; // 0.2 ms keeps the added delay small

    while (true) {
        natemu_service(n);

        if (next_rebind && now_ms() >= next_rebind) {
            natemu_rebind(n);
            next_rebind += rebind_every_ms;
        }

        nanosleep(&pause, NULL);
    }
}
