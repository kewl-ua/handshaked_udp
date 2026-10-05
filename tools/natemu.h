#ifndef NATEMU_H
#define NATEMU_H

#include <stdbool.h>
#include <stdint.h>

// Userspace CGNAT emulator: a UDP relay on 127.0.0.1 that sits between clients and one server and
// behaves like a carrier NAT. Mappings are endpoint-independent, inbound filtering is address-dependent,
// only outbound traffic refreshes a mapping, and an idle mapping expires. As with Starlink, the lossy
// link sits on the client side of the NAT: uplink packets are delayed before translation, downlink
// packets after it.

typedef struct natemu natemu_t;

typedef struct {
    uint32_t delay_ms;  // One-way base delay
    uint32_t jitter_ms; // Uniform +- spread around the delay, so packets can overtake each other
    double loss;        // Drop probability, 0..1
    double duplicate;   // Duplication probability, 0..1
} natemu_link_t;

typedef struct {
    uint16_t inside_port;        // Clients send here instead of to the server
    const char *server_ip;
    uint16_t server_port;
    uint32_t mapping_timeout_ms; // An idle mapping is dropped after this long
    natemu_link_t up;            // Client -> NAT
    natemu_link_t down;          // NAT -> client
    uint32_t seed;               // Same seed, same losses and delays
    bool verbose;                // Log mapping changes to stdout
} natemu_config_t;

typedef struct {
    uint64_t up_forwarded, up_lost;
    uint64_t down_forwarded, down_lost;
    uint64_t down_filtered; // Inbound from a foreign address
    uint64_t link_dropped;  // Dropped while the link was down, in flight ones included
    uint64_t mappings, expired, rebinds;
} natemu_stats_t;

// Defaults: inside port 5556 in front of 127.0.0.1:5555, 30 s mapping timeout, a clean link.
void natemu_config_default(natemu_config_t *cfg);

// Returns NULL and sets errno on failure.
natemu_t *natemu_new(const natemu_config_t *cfg);
void natemu_free(natemu_t *n);

// Forwards what arrived, releases delayed packets that are due and expires idle mappings. Never blocks.
void natemu_service(natemu_t *n);

// Drops every mapping now, as a CGNAT does on a handover: the next outbound packet gets a new public port.
void natemu_rebind(natemu_t *n);

// Takes the link down (everything in flight is lost) or brings it back.
void natemu_set_link(natemu_t *n, bool up);

// Public port of the first live mapping, or 0.
uint16_t natemu_public_port(const natemu_t *n);

natemu_stats_t natemu_stats(const natemu_t *n);

#endif
