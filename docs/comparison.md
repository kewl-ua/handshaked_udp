# Choosing HUDP, WireGuard or ICE

> [Docs](README.md) › Comparison

HUDP manages application datagram sessions, WireGuard tunnels IP traffic, and ICE discovers and checks paths between peers. They solve overlapping connectivity problems at different layers. This is a design comparison and packet-size calculation, **not a measured WireGuard/ICE benchmark**. Our [benchmark](benchmark.md) currently measures only HUDP and a fixed-address plain UDP application.

## Capabilities and Deployment

| Requirement | HUDP v1 | WireGuard | ICE over UDP |
|---|---|---|---|
| Main purpose | Session management for one application's datagrams | Encrypted IP tunnel for existing applications | Candidate gathering, connectivity checks and path selection |
| Client behind CGNAT, reachable public server | Fits the design | Fits when the client initiates to the configured endpoint | Can establish this path, though candidate negotiation may be unnecessary |
| Both peers behind NAT | No built-in peer-to-peer traversal | Needs a reachable endpoint or additional coordination; no built-in ICE/TURN fallback | Designed to test peer paths; TURN relay candidates provide a fallback when direct paths fail |
| Payload security | No encryption, authentication or replay protection | Encryption, peer authentication and replay protection | Connectivity checks are authenticated; application-data security is a separate layer |
| Maintaining connectivity | Application-driven service loop, keep-alives, migration and re-handshake | Authenticated endpoint roaming; configurable persistent keep-alive | Checks, keep-alives and ICE restart; recovery depends on integration |
| Integration | POSIX C API inside the application | Configure tunnel, addresses, routes and keys | Integrate an ICE implementation and a signaling channel to exchange candidates and credentials |
| Application delivery | UDP loss, duplicates and reordering remain possible | UDP inside the tunnel remains unreliable; TCP keeps TCP semantics | Depends on the transport using the selected path |

WireGuard provides [endpoint roaming](https://www.wireguard.com/) and [persistent keep-alive](https://www.wireguard.com/quickstart/); it does not need a relay for a client that can reach a public endpoint. Its data packets use UDP and do not retransmit application UDP datagrams. Encryption alone does not establish that HUDP has lower latency or CPU usage: those need measurements on the target hardware.

[ICE (RFC 8445)](https://www.rfc-editor.org/rfc/rfc8445.html) uses STUN checks to select a candidate pair. It is not inherently an encrypted transport, nor is TURN mandatory for every deployment. On a direct path, application UDP packets do not acquire an ICE header. Relaying via [TURN (RFC 8656)](https://www.rfc-editor.org/rfc/rfc8656.html) adds framing and a relay hop; WebRTC also adds its own security and media/data transports. Compare the actual stack, rather than treating ICE and WebRTC as interchangeable.

## Packet Overhead: A Small-Message Example

Assume a 26-byte application payload at 250 packets/s in one direction, IPv4 without options, UDP, no fragmentation, and one message per packet. Exclude link-layer framing, signaling, handshakes, keep-alives and application security/framing.

| Data path | Calculation | IP packet size | Bytes/s at 250 Hz |
|---|---|---:|---:|
| Plain UDP, or raw application UDP on a direct ICE-selected path | 20 IP + 8 UDP + 26 payload | 54 | 13,500 |
| HUDP v1 | 20 IP + 8 UDP + 2 HUDP + 26 payload | 56 | 14,000 |
| Application UDP inside WireGuard, inner and outer IPv4 | 20 outer IP + 8 outer UDP + 32 WireGuard + padded inner packet (64) | 124 | 31,000 |

The WireGuard row is calculated from its [data-packet format](https://www.wireguard.com/protocol/): 16 bytes of fields plus a 16-byte authentication tag, with the inner IP packet padded to a multiple of 16 bytes. The inner packet here is 20 + 8 + 26 = 54 bytes, padded to 64. IPv6, additional framing, batching and different payload sizes change the totals.

In this example HUDP uses 68 fewer bytes per packet than tunneled UDP: 17,000 bytes/s less per direction. **That saving buys fewer features, including no authenticated session migration.** Against raw UDP on a direct ICE path, HUDP adds 2 bytes; its advantage is a simpler fixed-server session model, not lower per-packet overhead. Adding security to HUDP also changes the comparison.

## When HUDP Makes Sense

Choose HUDP when these conditions match your application:

- You control the C application and already have a reachable public IPv4 server.
- You exchange small, frequent state messages and can tolerate packet loss; the application handles ordering and stale data itself.
- You want application-level connection/loss/migration events without configuring an IP tunnel or implementing candidate negotiation.
- You do not need confidentiality, and you have assessed authenticity and replay requirements separately. HUDP v1's 8-bit Session ID is not proof of identity; payload authentication alone does not authenticate its control packets or migration decisions.

For example, a POSIX device periodically sending non-sensitive sensor state to a known server may benefit from HUDP's small header and direct C API. A stream that already has application security may also be a candidate, provided session/control-plane risks are acceptable. Small overhead alone is not enough to choose an unauthenticated protocol for safety-critical commands.

Choose **WireGuard** when you need an authenticated encrypted network path, want several existing applications to use it without changes, or already operate a tunnel. It also handles NAT keep-alives and endpoint changes, so those features alone do not justify replacing it with HUDP.

Choose **ICE** when peers may both be behind NAT, you need to discover and select among multiple paths, need relay fallback, or need WebRTC interoperability. Lack of an encryption requirement is not a reason to reject ICE itself.

For reliable commands, files or transactions, also choose an appropriate reliable application transport. None of HUDP, an IP tunnel or ICE path selection alone guarantees application-message delivery.

## Application Examples

These are illustrative design choices, not deployments we have tested. The network topology and the application's requirements determine the choice.

### HUDP: Non-Sensitive Live Sensor Readings

A Linux measurement device on LTE sends a 32-byte snapshot of temperature, vibration and signal quality 100 times per second to a known public server. The dashboard needs the latest reading; missing one sample is acceptable, and historical completeness is not required. The server sends occasional requests for a fresh snapshot.

HUDP fits because both applications are under your control, the device can initiate the session, and a small header matters relative to the payload. The C application can react to `CONNECTED`, `LOST` and `MIGRATED` without setting up a tunnel. Put a sample counter or timestamp in the payload and reject stale snapshots in the receiver. Choose this design only when the readings are non-sensitive and accepting forged data or session disruption has acceptable consequences; a Session ID does not establish trust.

### HUDP: A Controlled Network Test Rig

A POSIX test program behind the bundled CGNAT emulator exchanges 26-byte synthetic state snapshots at 250 Hz with a public-server equivalent. The experiment needs to observe mapping expiry, port migration and reconnection, rather than route arbitrary IP traffic or protect real commands.

HUDP fits because its session events expose exactly those transitions, and the test can run in userspace without configuring a VPN interface. This is the kind of workload covered by our [scenario tests](development.md#tests) and [benchmark](benchmark.md). An application sequence number is still needed if the receiver must distinguish new snapshots from reordered ones.

### WireGuard: Remote Maintenance of a Linux Gateway

An LTE-connected gateway needs SSH access, software updates and a private monitoring endpoint. Administrators should use their existing tools, and credentials, configuration and traffic must be protected. The gateway initiates a tunnel to a reachable public WireGuard endpoint.

WireGuard fits because one authenticated encrypted IP path serves several applications without changing their code. Configure persistent keep-alive when inbound access must survive idle periods behind NAT. HUDP would require adapting each application and adding security; saving a few bytes on a sensor datagram does not solve this maintenance requirement.

### ICE: A Browser-to-Browser Call

Two users on home Wi-Fi or mobile networks want an audio/video call. Neither has a configured public endpoint. The application exchanges candidates and credentials through signaling, tries direct paths, and offers TURN when direct connectivity is unavailable.

ICE fits as part of a WebRTC stack because it checks possible peer paths instead of assuming a fixed public server. WebRTC supplies the media transport and security beyond ICE. HUDP cannot replace this with its current client-to-public-server design.

### Requirements Change the Answer

| Starting scenario | Changed requirement | Consequence |
|---|---|---|
| Live sensor snapshots using HUDP | Every sample must reach a historical archive | Add durable buffering and reliable delivery, or use an appropriate reliable transport; HUDP alone is insufficient. |
| Device-to-public-server session | Devices must communicate directly while both are behind NAT | Consider ICE and a signaling/relay deployment rather than assuming HUDP can discover a peer path. |
| Non-sensitive live state | Messages now authorize configuration changes or physical actions | Reassess authentication, replay protection and delivery semantics; HUDP v1 does not provide them. |
| Small datagrams to one server | SSH, HTTP and other unmodified applications also need access | A WireGuard tunnel may be simpler to operate than adapting every application to HUDP. |

The useful HUDP niche is a **controlled application, known public server, loss-tolerant live data and a reason to keep framing and integration small**. Not needing encryption helps that fit, but does not remove the separate question of whether forged messages and unauthenticated migration are acceptable.

## What We Have Not Measured

We have not run WireGuard or an ICE implementation through the emulator, measured cryptographic CPU cost, or compared their setup/recovery latency. The existing plain-UDP baseline deliberately has no keep-alives or endpoint migration; it does not represent WireGuard, ICE, or every UDP application. The numbers above describe packet size under stated assumptions, not a universal performance ranking.
