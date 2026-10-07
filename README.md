# Handshaked UDP

**Low-latency UDP through carrier-grade NAT, as a tiny C library.** A client behind CGNAT (Starlink, LTE/5G) and a server with a public IP keep a real-time stream alive in both directions: a handshake opens the path, keep-alives hold the NAT mapping, and connection migration follows the client when the NAT changes its port. No VPN, no relay, no STUN or TURN server.

[![CI](https://github.com/kewl-ua/handshaked_udp/actions/workflows/ci.yml/badge.svg)](https://github.com/kewl-ua/handshaked_udp/actions/workflows/ci.yml)
[![Release](https://img.shields.io/github/v/release/kewl-ua/handshaked_udp)](https://github.com/kewl-ua/handshaked_udp/releases/latest)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
![Language: C](https://img.shields.io/badge/language-C-blue)
![Platform: POSIX](https://img.shields.io/badge/platform-POSIX-lightgrey)

It is a minimalist network protocol with `libhudp`, a small C library (POSIX sockets) that implements it, for real-time state streams such as remote control and telemetry, sensor feeds or game state, where only the newest packet matters.

**[Getting Started](docs/getting-started.md)** · [Library Guide](docs/guide.md) · [API Reference](docs/api.md) · [Protocol](docs/protocol.md) · [CGNAT Emulator](docs/emulator.md) · [Benchmark](docs/benchmark.md) · [FAQ](docs/faq.md)

<p align="center">
  <img src="docs/handshaked-vs-udp.svg" width="100%" alt="Animation: plain UDP loses the client once the NAT mapping expires or the client's port changes, while Handshaked UDP keeps the mapping alive and follows the client to its new port">
</p>

## 🌐 Why

A device on Starlink or LTE/5G sits behind [carrier-grade NAT](https://en.wikipedia.org/wiki/Carrier-grade_NAT). It has no public address, so nothing can connect to it. The NAT closes UDP mappings that go idle and may change the device's port in the middle of a session. A fixed-address UDP application can lose the return stream. HUDP maintains an application session; WireGuard provides an encrypted IP tunnel; ICE selects peer paths. TCP provides reliable ordered delivery, which can delay fresh state behind lost data. → [The problem in detail](docs/protocol.md#the-problem)

## 🛠 How It Works

1. **The client knocks.** A `CONN_REQ` with a random Session ID opens a NAT mapping, and the server answers with `CONN_ACK`.
2. **Both sides stream** `MSG_DATA`. The server always answers at the address the latest packet came from.
3. **Keep-alives** after 100 ms of silence hold the mapping open, and the server answers each one at once.
4. **When the NAT changes the port**, the server follows the client by its Session ID ([connection migration](docs/protocol.md#connection-migration)). **After a link loss** the client knocks again by itself ([session lifetime](docs/protocol.md#session-lifetime)).

All of it on a [2-byte header](docs/protocol.md#packet-format), with no retransmissions. → [The protocol](docs/protocol.md)

## ⚖️ Plain UDP vs Handshaked UDP

Plain UDP here means an application that sends datagrams to a fixed peer address, with no handshake and no keep-alive.

|                                    | Plain UDP                                 | Handshaked UDP                                              |
|------------------------------------|-------------------------------------------|-------------------------------------------------------------|
| Server → client behind CGNAT       | ✗ dropped until the client sends first    | ✓ the client opens the path with `CONN_REQ`                 |
| Client has nothing to send         | ✗ the NAT mapping expires                 | ✓ keep-alives hold the mapping open                         |
| NAT changes the client's port      | ✗ the server keeps sending to the old one | ✓ the server matches the `Session ID` and follows the client |
| Header overhead                    | 0 bytes                                   | 2 bytes                                                     |
| Retransmissions                    | none                                      | none: stale packets are never resent                        |

Measured behind the [CGNAT emulator](docs/emulator.md), 250 Hz both ways, 20 ± 5 ms and 1 % loss, the share of the server's stream that reached the client:

| Scenario                     | Handshaked UDP                              | Plain UDP                 |
|------------------------------|---------------------------------------------|---------------------------|
| Steady                       | 98.4 %                                      | 98.5 %                    |
| NAT port change every 2 s    | 98.5 %, longest gap 14 ms                   | 24.2 %, never recovered   |
| Client idle for 4 s          | 98.5 %, longest gap 14 ms                   | 49.2 %, never recovered   |
| 2 s link outage              | back about 0.3 s after the link returns     | never recovered           |

→ [Full benchmark](docs/benchmark.md) · [HUDP vs WireGuard vs ICE](docs/comparison.md)

## Choosing HUDP, WireGuard or ICE

| Choose | When it fits |
|---|---|
| **HUDP** | A C application sending small, loss-tolerant datagrams to a known public server; you want session events and minimal framing, and can accept v1's lack of authentication and encryption. |
| **WireGuard** | An authenticated encrypted IP tunnel for existing applications, with NAT keep-alives and authenticated endpoint roaming. |
| **ICE** | Discovering and checking peer paths, including peers behind NAT, with TURN relay candidates when direct connectivity fails. |

HUDP adds **2 bytes** per datagram. WireGuard carries an inner IP packet plus encrypted tunnel framing. ICE itself does **not** encrypt application data or add a header to every direct-path UDP message. HUDP's simpler deployment can be useful even when packet-size savings are small; lack of encryption is a tradeoff, not evidence of lower latency.

For our 26-byte payload example over IPv4, calculated packet sizes are **56 bytes for HUDP**, **124 for UDP inside WireGuard**, and **54 for raw UDP on a direct ICE-selected path**. These are packet-size calculations, not measured WireGuard/ICE benchmarks. See [assumptions, official sources and selection criteria](docs/comparison.md).

Examples of that choice:

- **HUDP:** a Linux sensor on LTE sends non-sensitive live readings to your public server; missed samples are acceptable and the C application handles stale data. Also fits a controlled NAT test rig exchanging synthetic state.
- **WireGuard:** an LTE gateway needs protected SSH, updates and monitoring through one tunnel, using existing applications.
- **ICE / WebRTC:** two browsers behind different NATs need a call, with direct-path discovery and TURN fallback.

See [worked application scenarios and how changing requirements changes the choice](docs/comparison.md#application-examples).

## 🚀 Quick Start

```sh
git clone https://github.com/kewl-ua/handshaked_udp.git
cd handshaked_udp
make
./bin/server &              # on the host with the public IP
./bin/client 127.0.0.1      # behind CGNAT: pass the server's public IP
```

Your own client is one loop: take the events, send your state, sleep until the next tick.

```c
hudp_t *client = hudp_client_new("203.0.113.10", HUDP_DEFAULT_PORT, NULL);

for (;;) {
    hudp_event_t ev;

    while (hudp_service(client, &ev, 0) > 0) {   // handshake, keep-alives, reconnects happen in here
        if (ev.type == HUDP_EVENT_DATA) {
            // ev.data, ev.len: a payload from the server
        }
    }

    if (hudp_state(client) == HUDP_STATE_CONNECTED) {
        hudp_send(client, state, sizeof(state));
    }

    sleep_until_next_tick();
}
```

→ A complete echo server and client in [Getting Started](docs/getting-started.md#quick-start-an-echo-server-and-a-client), recipes in the [Library Guide](docs/guide.md), every function in the [API Reference](docs/api.md).

## 📚 Documentation

| Page                                        | What is in it                                                       |
|---------------------------------------------|---------------------------------------------------------------------|
| [Getting Started](docs/getting-started.md)  | build, run the examples, a first echo server and client             |
| [Library Guide](docs/guide.md)              | the event loop, waiting, a 250 Hz stream, `poll()`, timings, pitfalls |
| [API Reference](docs/api.md)                | every function, event, state, error and constant                    |
| [Protocol](docs/protocol.md)                | the problem, handshake, migration, session lifetime, packet format, RFCs |
| [CGNAT Emulator](docs/emulator.md)          | test on one machine with port changes, expiring mappings, delay and loss |
| [Comparison](docs/comparison.md) | HUDP, WireGuard and ICE: topology, security, packet overhead and when to use each |
| [Benchmark](docs/benchmark.md)              | hudp against plain UDP in five scenarios                            |
| [Development](docs/development.md)          | repository layout, make targets, tests, CI, Windows                 |
| [FAQ](docs/faq.md)                          | Starlink, STUN/TURN/ICE, VPNs, symmetric NAT, security, bandwidth   |

## ❓ FAQ

- [How do I reach a device behind Starlink or mobile CGNAT from the internet?](docs/faq.md#how-do-i-reach-a-device-behind-starlink-or-mobile-cgnat-from-the-internet)
- [How is this different from STUN, TURN and ICE (WebRTC)?](docs/faq.md#how-is-this-different-from-stun-turn-and-ice-webrtc)
- [Why not a VPN such as WireGuard or Tailscale, or just TCP?](docs/faq.md#why-not-a-vpn-such-as-wireguard-or-tailscale-or-just-tcp)
- [Is it encrypted or authenticated?](docs/faq.md#is-it-encrypted-or-authenticated)
- [All questions →](docs/faq.md)

## 📄 License

[MIT](LICENSE): use it in open or closed source projects, keep the copyright notice.
