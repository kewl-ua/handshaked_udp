# Getting Started

> [Docs](README.md) › Getting Started

Build the library, run the bundled client and server, then write your first program with it.

## Build

You need `make` and `gcc` or `clang` on Linux or another POSIX system:

```sh
git clone https://github.com/kewl-ua/handshaked_udp.git
cd handshaked_udp
make            # lib/libhudp.a, the examples bin/server and bin/client, and bin/natemu
make test       # optional: every test, see Development
```

On Windows, build in the **MSYS** shell of [MSYS2](https://www.msys2.org/) after `pacman -S gcc make`. The MinGW environments have no POSIX sockets. More on the targets and tests in [Development](development.md).

## Run the Examples

[`examples/server.c`](../examples/server.c) answers every packet of the client, and [`examples/client.c`](../examples/client.c) streams at 250 Hz. Run the server on the host with the public IP, and the client behind CGNAT with the server's address:

```sh
./bin/server                # listens on UDP 5555; ./bin/server 6000 for another port
./bin/client 203.0.113.10   # ./bin/client 203.0.113.10 6000 for another port
```

No CGNAT at hand? Put the [CGNAT emulator](emulator.md) between them on one machine and watch the server follow the client across port changes.

## Quick Start: an Echo Server and a Client

Two complete programs: the server sends every payload back, the client sends a numbered message once a second and prints the echoes. They use [`hudp_server_new()`](api.md#hudp_server_new), [`hudp_client_new()`](api.md#hudp_client_new), [`hudp_service()`](api.md#hudp_service), [`hudp_send()`](api.md#hudp_send) and [`hudp_free()`](api.md#hudp_free), plus [`hudp_state()`](api.md#hudp_state) and [`hudp_peer()`](api.md#hudp_peer) to look around. After `make`, build them like this:

```sh
gcc -Wall -I include echo_server.c lib/libhudp.a -o echo_server
gcc -Wall -I include echo_client.c lib/libhudp.a -o echo_client
```

```c
// echo_server.c: sends every payload back to the client
#include <stdio.h>

#include "hudp.h"

int main(void) {
    hudp_t *server = hudp_server_new(HUDP_DEFAULT_PORT, NULL); // UDP 5555, default timings

    if (!server) {
        perror("hudp_server_new");
        return 1;
    }

    printf("Waiting for a client on port %d...\n", HUDP_DEFAULT_PORT);

    while (1) {
        hudp_event_t ev;
        int res = hudp_service(server, &ev, -1); // Sleep until something happens

        if (res < 0) {
            perror("hudp_service");
            break;
        }

        if (res == 0) {
            continue; // No event: nothing to do
        }

        switch (ev.type) {
        case HUDP_EVENT_CONNECTED:
            printf("Client connected, session %d\n", ev.session_id);
            break;
        case HUDP_EVENT_DATA:
            hudp_send(server, ev.data, ev.len); // Goes to the client's latest address
            break;
        case HUDP_EVENT_MIGRATED: {
            char addr[32];

            hudp_peer(server, addr, sizeof(addr));
            printf("The NAT gave the client a new port, now %s\n", addr);
            break;
        }
        case HUDP_EVENT_LOST:
            printf("Client lost, waiting for the next one\n");
            break;
        default:
            break;
        }
    }

    hudp_free(server);
    return 0;
}
```

```c
// echo_client.c: sends a numbered message once a second and prints what comes back
#include <stdio.h>
#include <time.h>

#include "hudp.h"

int main(int argc, char *argv[]) {
    const char *server_ip = argc > 1 ? argv[1] : "127.0.0.1";
    hudp_t *client = hudp_client_new(server_ip, HUDP_DEFAULT_PORT, NULL);

    if (!client) {
        perror("hudp_client_new"); // EINVAL: server_ip is not a valid IPv4 address
        return 1;
    }

    time_t next_send = 0;
    unsigned counter = 0;

    while (1) {
        hudp_event_t ev;
        int res = hudp_service(client, &ev, 100); // Wait up to 100 ms for an event

        if (res < 0) {
            perror("hudp_service");
            break;
        }

        if (res > 0) {
            switch (ev.type) {
            case HUDP_EVENT_CONNECTED:
                printf("Connected, session %d\n", ev.session_id);
                break;
            case HUDP_EVENT_DATA:
                printf("Echo: %.*s\n", (int)ev.len, (const char *)ev.data);
                break;
            case HUDP_EVENT_LOST:
                printf("Server lost, reconnecting...\n"); // The library knocks again by itself
                break;
            default:
                break;
            }
        }

        // hudp_send() fails with ENOTCONN until the handshake is done, so check the state first
        if (hudp_state(client) == HUDP_STATE_CONNECTED && time(NULL) >= next_send) {
            char msg[32];
            int len = snprintf(msg, sizeof(msg), "hello #%u", counter++);

            hudp_send(client, msg, (size_t)len);
            next_send = time(NULL) + 1;
        }
    }

    hudp_free(client);
    return 0;
}
```

Start the server, then the client. Now stop the server: a second later the client prints `Server lost, reconnecting...`. Start the server again and the client prints `Connected` with a new session and carries on, without any reconnect code of yours. Messages sent while the server was down are simply gone.

## Adding It to Your Project

Build the static library and link it:

```sh
make                                            # builds lib/libhudp.a
gcc -Wall -I handshaked_udp/include app.c handshaked_udp/lib/libhudp.a -o app
```

Or copy [`include/hudp.h`](../include/hudp.h), [`src/hudp.c`](../src/hudp.c) and [`src/protocol.h`](../src/protocol.h) into your source tree and compile `hudp.c` with the rest of your code. It needs nothing but libc and POSIX sockets.

## Next Steps

- [Library Guide](guide.md): how to wait, fixed-rate streams, `poll()`, your own timings.
- [API Reference](api.md): every function, event and error.
- [Protocol](protocol.md): what goes over the wire and why.
