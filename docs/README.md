# Handshaked UDP Documentation

> [Handshaked UDP](../README.md) › Docs

Start at the top and stop when you have what you need.

| Page                                   | Read it to                                                              |
|----------------------------------------|-------------------------------------------------------------------------|
| [Getting Started](getting-started.md)  | build the library, run the examples, write a first echo server and client |
| [Library Guide](guide.md)              | understand the event loop, choose how to wait, follow the recipes        |
| [API Reference](api.md)                | look up a function, an event, an error or a timing                       |
| [Protocol](protocol.md)                | see why plain UDP fails behind CGNAT and what goes over the wire         |
| [CGNAT Emulator](emulator.md)          | test on one machine with port changes, expiring mappings, delay and loss |
| [Comparison](comparison.md) | choose between HUDP, WireGuard and ICE using topology, security and overhead |
| [Benchmark](benchmark.md)              | compare hudp with plain UDP under five network conditions               |
| [Development](development.md)          | find your way around the repository, the tests and CI                   |
| [FAQ](faq.md)                          | get short answers: Starlink, STUN/TURN, VPNs, security, bandwidth       |

## By Question

- *How do I get a stream from my device behind Starlink or LTE to my server?* [Getting Started](getting-started.md), then the [250 Hz recipe](guide.md#recipe-a-250-hz-stream-where-only-the-newest-packet-counts).
- *What does `hudp_service()` return, and how long does it wait?* [`hudp_service()`](api.md#hudp_service) and [Waiting](guide.md#waiting-the-timeout_ms-of-hudp_service).
- *What happens when the NAT changes the port?* [Connection Migration](protocol.md#connection-migration).
- *What if the link drops?* [Session Lifetime](protocol.md#session-lifetime).
- *How do I combine it with a serial port or other sockets?* [The `poll()` recipe](guide.md#recipe-next-to-other-input-and-output-with-poll).
- *Can I trust the numbers?* [Benchmark](benchmark.md), and how the [emulator](emulator.md#what-it-models) models a carrier NAT.
