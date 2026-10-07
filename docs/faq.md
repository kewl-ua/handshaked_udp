# FAQ

> [Docs](README.md) › FAQ

## How do I reach a device behind Starlink or mobile CGNAT from the internet?

You can't connect to it directly: it has no public address of its own, and the carrier's NAT drops unsolicited inbound packets. Let the device connect out to a server with a public IP and keep that path open. That is what Handshaked UDP does ([how](protocol.md#how-it-works)), and from then on the server can send to the device at any time.

## How is this different from STUN, TURN and ICE (WebRTC)?

[ICE](https://www.rfc-editor.org/rfc/rfc8445.html) gathers candidates and checks paths between peers, including peers behind NAT; [TURN](https://www.rfc-editor.org/rfc/rfc8656.html) can supply relay candidates. ICE itself neither encrypts application data nor adds a per-message header on a direct UDP path. HUDP assumes a known public server and manages a session with a [2-byte header](protocol.md#packet-format), avoiding candidate negotiation. WebRTC adds other transports and security on top of ICE. See [the comparison](comparison.md).

## Why not a VPN such as WireGuard or Tailscale, or just TCP?

[WireGuard](https://www.wireguard.com/) offers encryption, authentication and endpoint roaming, with [persistent keep-alive](https://www.wireguard.com/quickstart/) for NAT. A client reaching a public WireGuard server needs no relay, and UDP inside the tunnel remains unreliable. HUDP can make sense when you control the application, need a small datagram session API rather than an IP tunnel, and can accept its security limitations. TCP retransmits lost data and preserves order, which can delay fresh state. See [selection criteria and packet overhead](comparison.md).

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
