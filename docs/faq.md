# FAQ

> [Docs](README.md) › FAQ

## How do I reach a device behind Starlink or mobile CGNAT from the internet?

You can't connect to it directly: it has no public address of its own, and the carrier's NAT drops unsolicited inbound packets. Let the device connect out to a server with a public IP and keep that path open. That is what Handshaked UDP does ([how](protocol.md#how-it-works)), and from then on the server can send to the device at any time.

## How is this different from STUN, TURN and ICE (WebRTC)?

[ICE](https://www.rfc-editor.org/rfc/rfc8445) connects two peers that are both behind NAT: [STUN](https://www.rfc-editor.org/rfc/rfc8489) to learn public addresses, [TURN](https://www.rfc-editor.org/rfc/rfc8656) to relay when hole punching fails. Here one side has a public IP, so a client-initiated session is enough. There is no STUN or TURN server to run, and the header is [2 bytes](protocol.md#packet-format).

## Why not a VPN such as WireGuard or Tailscale, or just TCP?

A VPN works, but it is another layer to run and keep up, and when its own hole punching fails the traffic goes through a relay. TCP retransmits lost segments and holds back everything behind them, so a stale packet delays the fresh ones. For a stream where only the newest state matters, a lost packet should simply be skipped, which plain UDP does.

## Does it work behind a symmetric NAT?

Yes. The client only ever talks to one server, and the server answers at whatever address the client's packets come from, so it does not matter how the NAT picks ports.

## What happens when the NAT changes the client's port?

The server notices on the next packet of the session and answers at the new address ([connection migration](protocol.md#connection-migration), reported as [`HUDP_EVENT_MIGRATED`](api.md#events)). In the [benchmark](benchmark.md) a port change costs no more than ordinary jitter.

## What happens when the link drops for a while?

After 1 s of silence both sides report [`HUDP_EVENT_LOST`](api.md#events). The client keeps knocking with a new session, and the server accepts it as soon as packets get through again: in the [benchmark](benchmark.md#results) that is about 0.3 s after the link returns.

## Is it encrypted or authenticated?

Not yet. Session IDs keep sessions apart, but they are not a security measure. Use it where that is acceptable, or authenticate inside your payload. Sequence numbers and authentication are planned for wire format v2.

## How much bandwidth does it use?

The protocol adds 2 bytes per packet. A 250 Hz stream of 26-byte payloads takes about 14 kB/s each way, IP and UDP headers included. An idle session sends 10 keep-alives a second.

## Which platforms does it run on?

Any POSIX system with BSD sockets. It is tested on Linux (CI) and on Windows in the MSYS environment of [MSYS2](https://www.msys2.org/). Build instructions: [Getting Started](getting-started.md#build).

## How can I test it without a real CGNAT?

With the bundled [CGNAT emulator](emulator.md): it changes ports, expires mappings and adds delay, jitter and loss on one machine.
