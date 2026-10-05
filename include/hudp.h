#ifndef HUDP_H
#define HUDP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HUDP_DEFAULT_PORT 5555
#define HUDP_MAX_PAYLOAD 1200 // Keeps a whole packet well below a 1500-byte MTU

typedef struct hudp hudp_t;

typedef enum {
    HUDP_STATE_LISTENING,  // Server: no session, waiting for CONN_REQ
    HUDP_STATE_CONNECTING, // Client: sending CONN_REQ, waiting for CONN_ACK
    HUDP_STATE_CONNECTED,  // Session established, MSG_DATA flows both ways
} hudp_state_t;

typedef enum {
    HUDP_EVENT_NONE,
    HUDP_EVENT_CONNECTED, // Session established
    HUDP_EVENT_DATA,      // Payload received: data / len
    HUDP_EVENT_MIGRATED,  // Server: the client's address changed, replies now follow it
    HUDP_EVENT_LOST,      // Peer silent for link_timeout_ms; a client starts a new handshake
} hudp_event_type_t;

typedef struct {
    hudp_event_type_t type;
    uint8_t session_id;
    const uint8_t *data; // HUDP_EVENT_DATA only, valid until the next hudp_service() call
    size_t len;
} hudp_event_t;

typedef struct {
    uint32_t handshake_interval_ms; // Client resends CONN_REQ this often (default 250)
    uint32_t keepalive_interval_ms; // Empty MSG_DATA after this much send silence (default 100)
    uint32_t link_timeout_ms;       // Peer is lost after this much receive silence (default 1000)
} hudp_config_t;

// Fills cfg with the defaults above.
void hudp_config_default(hudp_config_t *cfg);

// Create an endpoint; cfg may be NULL for the defaults.
// Return NULL and set errno on failure (EINVAL for a malformed server_ip).
hudp_t *hudp_client_new(const char *server_ip, uint16_t port, const hudp_config_t *cfg);
hudp_t *hudp_server_new(uint16_t port, const hudp_config_t *cfg);
void hudp_free(hudp_t *h);

// Runs the protocol: reads datagrams, resends CONN_REQ, sends keep-alives and detects link loss.
// Waits up to timeout_ms for an event: 0 returns at once, a negative value waits until one happens.
// Returns 1 and fills ev on an event, 0 on timeout, -1 on a socket error (errno set).
int hudp_service(hudp_t *h, hudp_event_t *ev, int timeout_ms);

// Sends one MSG_DATA to the peer. Returns 0, or -1 with errno: ENOTCONN before the handshake,
// EMSGSIZE above HUDP_MAX_PAYLOAD, or the socket error. An empty payload is a keep-alive and is not delivered.
int hudp_send(hudp_t *h, const void *payload, size_t len);

hudp_state_t hudp_state(const hudp_t *h);

// Writes the peer address as "ip:port": the server for a client, the client's latest address for a server.
// Returns 0, or -1 when there is no peer yet.
int hudp_peer(const hudp_t *h, char *buf, size_t cap);

// Socket descriptor, for poll()/epoll() integration.
int hudp_fd(const hudp_t *h);

#ifdef __cplusplus
}
#endif

#endif
