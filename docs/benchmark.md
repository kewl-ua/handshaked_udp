# Benchmark

> [Docs](README.md) › Benchmark

How Handshaked UDP holds a real-time stream through carrier-grade NAT, next to plain UDP in the same conditions. Both run behind the [CGNAT emulator](emulator.md).

## What It Measures

[`bench/bench.c`](../bench/bench.c) streams at 250 Hz in both directions at once, like control one way and telemetry the other, with 26-byte payloads, for 8 s per case. Every packet carries its sequence number and its send time. Client, server and emulator run in one process on one clock, so the receiving side measures directly:

- **delivered:** the share of the stream that arrived, duplicates not counted;
- **latency:** one-way, p50 and p99;
- **first down packet:** how long the server's stream takes to reach the client after the start;
- **longest down gap:** the longest silence in the server's stream, "never recovered" when it never resumed.

**Plain UDP** here means an application that sends datagrams to a fixed peer address with no handshake and no keep-alive: the server keeps the first client address it saw.

## Scenarios

| Scenario                          | Link                                                    |
|-----------------------------------|---------------------------------------------------------|
| Clean link                        | no delay, no loss                                       |
| Starlink-like                     | 20 ± 5 ms each way, 1 % loss each way                   |
| + NAT port change every 2 s       | the emulator drops every mapping at 2, 4 and 6 s        |
| + client idle for 4 s             | the client application sends nothing from 2 to 6 s; the server keeps streaming |
| + 2 s link outage                 | nothing gets through from 3 to 5 s                      |

The NAT mapping timeout is compressed to 2 s so a case fits into 8 s; carrier NATs keep idle mappings for tens of seconds or minutes.

## Results

One local run, Windows 11 + MSYS2, GCC 15.3. The emulator is seeded, and the CI run on Ubuntu gives the same numbers.

| Scenario | Protocol | Up delivered | Down delivered | Down latency p50 / p99 | First down packet | Longest down gap | hudp events |
|---|---|---|---|---|---|---|---|
| Clean link | hudp | 100.0 % | 100.0 % | 0.0 / 0.1 ms | 4 ms | 4 ms | — |
|  | plain UDP | 100.0 % | 100.0 % | 0.0 / 0.1 ms | 4 ms | 4 ms | — |
| Starlink-like: 20 ± 5 ms, 1 % loss | hudp | 98.5 % | 98.4 % | 19.9 / 24.9 ms | 54 ms | 15 ms | — |
|  | plain UDP | 99.1 % | 98.5 % | 19.9 / 24.9 ms | 40 ms | 15 ms | — |
| + NAT port change every 2 s | hudp | 98.3 % | 98.5 % | 20.0 / 24.9 ms | 54 ms | 14 ms | 3 migrations |
|  | plain UDP | 99.0 % | 24.2 % | 19.6 / 24.9 ms | 40 ms | never recovered | — |
| + client idle for 4 s | hudp | 97.4 % | 98.5 % | 20.0 / 24.9 ms | 54 ms | 14 ms | — |
|  | plain UDP | 98.8 % | 49.2 % | 19.9 / 24.9 ms | 40 ms | never recovered | — |
| + 2 s link outage | hudp | 69.8 % | 69.9 % | 19.9 / 24.9 ms | 54 ms | 2304 ms | 1 lost, 1 reconnects |
|  | plain UDP | 74.0 % | 36.3 % | 19.7 / 24.9 ms | 40 ms | never recovered | — |

## Reading the Table

- **The handshake costs about 14 ms** before the first packet (54 ms against 40 ms). The ticks before it count as not delivered, which is why hudp's shares start a little lower.
- **A port change costs no more than ordinary jitter:** the longest gap stays at 14 ms, because the next client packet tells the server the new port ([connection migration](protocol.md#connection-migration)). Plain UDP keeps sending to the old port: its downlink stops at 2 s, which is the 24 %.
- **Keep-alives hold the mapping through silence.** Plain UDP's mapping expires 2 s into the client's idle time, at 4 s: 49 %.
- **After an outage hudp reconnects about 0.3 s after the link returns:** the 2.3 s gap is the 2 s outage, plus the wait for the next `CONN_REQ` and its round trip. Plain UDP's mapping has expired by then.
- Plain UDP's uplink survives everything: the client keeps sending to the server's fixed address. It is the downlink that dies.

## Running It

```sh
make bench          # 5 scenarios × 2 protocols × 8 s, about 80 s
./bin/bench 3       # 3 s per case for a quick look
```

The loop busy-waits on purpose, since sleeping would add the scheduler's granularity to every latency, so it keeps one CPU core busy. [CI](development.md#continuous-integration) runs it on every push and publishes the table in the run summary.
