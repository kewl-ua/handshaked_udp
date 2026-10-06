# CGNAT Emulator

> [Docs](README.md) › CGNAT Emulator

`natemu` is a userspace carrier-grade NAT for testing on a single machine, without Starlink or a SIM card. It is a UDP relay on `127.0.0.1`: clients send to it instead of to the server, and it translates their packets the way a carrier NAT would. The [tests](development.md#tests) and the [benchmark](benchmark.md) embed it, and `bin/natemu` runs it in front of any UDP server.

## What It Models

In the terms of [RFC 4787](https://www.rfc-editor.org/rfc/rfc4787):

- **Endpoint-independent mapping.** Each client address gets its own public port, the same whatever it sends to.
- **Address-dependent filtering.** A mapping only lets in packets from addresses it has sent to.
- **Outbound refresh only.** Only the client's packets keep a mapping alive; packets from the server do not.
- **Idle timeout.** A mapping that has seen no outbound packet for the timeout is dropped, and the client's next packet gets a new public port.
- **Port change on demand.** Every mapping can be dropped at once, as a carrier NAT does on a handover.
- **Link outage on demand.** Everything in flight and everything sent while the link is down is lost.

The lossy link sits on the client side of the NAT, as with Starlink, where the NAT is in the ground station after the satellite hop. Uplink packets are delayed before translation, downlink packets after it. Delay, jitter (packets may overtake each other), loss and duplication come from a seeded generator, so the same seed gives the same run.

## Running It

```sh
./bin/server 6000 &
./bin/natemu --listen 5555 --server 127.0.0.1:6000 --delay 20 --jitter 5 --loss 1 --rebind-every 3000 &
./bin/client 127.0.0.1      # the server logs "Client moved to ..." every 3 s and the session holds
```

The emulator logs every mapping it creates, expires or drops:

```text
[NAT] Listening on 127.0.0.1:5555, forwarding to 127.0.0.1:6000
[NAT] Mapping 127.0.0.1:61438 -> :61440 created
[NAT] Mapping 127.0.0.1:61438 -> :61440 dropped by rebind
[NAT] Mapping 127.0.0.1:61438 -> :61447 created
```

| Option               | Default           | Meaning                                                  |
|----------------------|-------------------|----------------------------------------------------------|
| `--listen PORT`      | 5556              | the port clients send to                                 |
| `--server IP:PORT`   | `127.0.0.1:5555`  | the real server                                          |
| `--timeout MS`       | 30000             | idle mapping timeout                                     |
| `--delay MS`         | 0                 | one-way delay, each direction                            |
| `--jitter MS`        | 0                 | ± spread around the delay                                |
| `--loss PCT`         | 0                 | packet loss in percent, each direction                   |
| `--dup PCT`          | 0                 | packet duplication in percent, each direction            |
| `--rebind-every MS`  | never             | drop all mappings periodically, like a handover          |
| `--seed N`           | 1                 | random seed                                              |

## Embedding It

[`tools/natemu.h`](../tools/natemu.h) is a small C API, which is how [`tests/test_nat.c`](../tests/test_nat.c) and [`bench/bench.c`](../bench/bench.c) use it:

| Function                  | What it does                                                          |
|---------------------------|-----------------------------------------------------------------------|
| `natemu_config_default()` | fills a config: port 5556 in front of `127.0.0.1:5555`, 30 s timeout, clean link |
| `natemu_new()`            | starts an emulator, `NULL` with `errno` on failure                    |
| `natemu_service()`        | forwards what arrived and releases delayed packets; never blocks      |
| `natemu_rebind()`         | drops every mapping now, so the next packet gets a new public port    |
| `natemu_set_link()`       | takes the link down or brings it back                                 |
| `natemu_public_port()`    | the public port of the first live mapping, or 0                       |
| `natemu_stats()`          | counters: forwarded, lost, filtered, mappings, expirations, rebinds   |
| `natemu_free()`           | stops it                                                              |

Call `natemu_service()` in the same loop as `hudp_service()`, as often as you can: the delays are only as precise as that loop.

## Limits

The emulator follows one common carrier NAT behaviour. Real ones differ, and some details are simplified:

- Port allocation, port preservation and per-destination (symmetric) mappings are not modelled. With one server per client this does not change the outcome.
- A dropped mapping closes its socket, so packets to it make the host answer with ICMP "port unreachable". A real CGNAT drops them silently. The library treats both the same way.
- No IPv6, no hairpinning, a single public IP (`127.0.0.1`).
