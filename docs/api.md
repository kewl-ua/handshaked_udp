# API Reference

> [Docs](README.md) › API Reference

Everything is declared in [`include/hudp.h`](../include/hudp.h). Functions that can fail return `NULL` or `-1` and set `errno`. For how the pieces fit together, read the [Library Guide](guide.md) first.

| Function                                        | What it does                                        |
|-------------------------------------------------|-----------------------------------------------------|
| [`hudp_client_new()`](#hudp_client_new)         | creates a client that connects to a server          |
| [`hudp_server_new()`](#hudp_server_new)         | creates a server that waits for a client            |
| [`hudp_free()`](#hudp_free)                     | closes and frees an endpoint                        |
| [`hudp_config_default()`](#hudp_config_default) | fills a config with the default timings             |
| [`hudp_service()`](#hudp_service)               | runs the protocol and returns the next event        |
| [`hudp_send()`](#hudp_send)                     | sends one packet                                    |
| [`hudp_state()`](#hudp_state)                   | tells where the session stands                      |
| [`hudp_peer()`](#hudp_peer)                     | writes the peer address as text                     |
| [`hudp_fd()`](#hudp_fd)                         | the socket, for `poll()` and `epoll`                |

Types: [events](#events), [states](#states), [configuration](#configuration), [constants](#constants).

## Creating and Destroying

### hudp_client_new

```c
hudp_t *hudp_client_new(const char *server_ip, uint16_t port, const hudp_config_t *cfg);
```

Creates a client for the server at `server_ip` (dotted IPv4, like `"203.0.113.10"`) and `port`. Pass `NULL` as `cfg` for the default timings. Nothing is sent yet: the first [`hudp_service()`](#hudp_service) call sends the first `CONN_REQ` of the [handshake](protocol.md#communication-flow). The client starts in `HUDP_STATE_CONNECTING`.

Returns `NULL` on failure, with `errno` set to `EINVAL` for a malformed address or to the socket error.

### hudp_server_new

```c
hudp_t *hudp_server_new(uint16_t port, const hudp_config_t *cfg);
```

Creates a server that listens on `port` on all interfaces. It starts in `HUDP_STATE_LISTENING` and takes one client at a time.

Returns `NULL` on failure, for example with `errno` set to `EADDRINUSE` when the port is taken.

### hudp_free

```c
void hudp_free(hudp_t *h);
```

Closes the socket and frees the endpoint. `NULL` is fine. Nothing is sent to the peer: it notices through its link timeout.

### hudp_config_default

```c
void hudp_config_default(hudp_config_t *cfg);
```

Fills `cfg` with the default timings, so you only change the fields you care about. See [configuration](#configuration).

## Running

### hudp_service

```c
int hudp_service(hudp_t *h, hudp_event_t *ev, int timeout_ms);
```

Does the protocol work and reports what happened: reads incoming packets, repeats `CONN_REQ` while connecting, sends keep-alives, answers the client's keep-alives, notices a lost peer and follows a client to a new port.

| Returns | Meaning                                                        |
|---------|----------------------------------------------------------------|
| `1`     | `ev` holds an [event](#events)                                 |
| `0`     | `timeout_ms` ran out without an event; `ev` is left untouched  |
| `-1`    | socket error, see `errno`                                      |

`timeout_ms` is `0` to return at once, a positive number of milliseconds to wait at most, or `-1` to wait until an event happens. One event per call. How to choose: [Waiting](guide.md#waiting-the-timeout_ms-of-hudp_service).

## Sending

### hudp_send

```c
int hudp_send(hudp_t *h, const void *payload, size_t len);
```

Sends one packet of `len` bytes, up to [`HUDP_MAX_PAYLOAD`](#constants). A server sends to the client's latest address. Returns `0` when the packet has left. That says nothing about it arriving: there is no acknowledgement and no retransmission. Returns `-1` with `errno`:

| `errno`       | Meaning                                                                   |
|---------------|---------------------------------------------------------------------------|
| `ENOTCONN`    | no session: the client is still handshaking, or the server has no client  |
| `EMSGSIZE`    | `len` is above `HUDP_MAX_PAYLOAD`                                         |
| anything else | the socket error, e.g. `ECONNREFUSED` while the server is down; it passes  |

An empty payload (`len` 0) is a keep-alive: it keeps the session alive but never shows up as `HUDP_EVENT_DATA`.

## Inspecting

### hudp_state

```c
hudp_state_t hudp_state(const hudp_t *h);
```

The current [state](#states). Check for `HUDP_STATE_CONNECTED` before sending.

### hudp_peer

```c
int hudp_peer(const hudp_t *h, char *buf, size_t cap);
```

Writes the peer address as `"ip:port"`: the server's for a client, the client's latest for a server. 22 bytes are always enough. Returns `0`, or `-1` when there is no peer yet.

### hudp_fd

```c
int hudp_fd(const hudp_t *h);
```

The socket descriptor, to wait on with `poll()`, `select()` or `epoll` together with your own descriptors. Never read from it or close it yourself. Example: [the `poll()` recipe](guide.md#recipe-next-to-other-input-and-output-with-poll).

## Events

`hudp_service()` fills an `hudp_event_t`:

| Field        | Meaning                                                                  |
|--------------|--------------------------------------------------------------------------|
| `type`       | one of the events below                                                  |
| `session_id` | the session the event belongs to (1 to 255)                              |
| `data`       | `HUDP_EVENT_DATA` only: the payload, valid until the next `hudp_service()` |
| `len`        | `HUDP_EVENT_DATA` only: the payload size in bytes                        |

| Event                  | Client | Server | When                                                                                   |
|------------------------|:------:|:------:|----------------------------------------------------------------------------------------|
| `HUDP_EVENT_CONNECTED` | ✓      | ✓      | the handshake is done and data can flow                                                |
| `HUDP_EVENT_DATA`      | ✓      | ✓      | a payload arrived                                                                      |
| `HUDP_EVENT_MIGRATED`  |        | ✓      | the client's address changed ([connection migration](protocol.md#connection-migration)); replies already go to the new one |
| `HUDP_EVENT_LOST`      | ✓      | ✓      | nothing heard for `link_timeout_ms`; a client starts a new handshake by itself, a server waits for a client |

## States

`hudp_state()` returns an `hudp_state_t`:

| State                   | Who    | Meaning                                         |
|-------------------------|--------|-------------------------------------------------|
| `HUDP_STATE_LISTENING`  | server | waiting for a client                            |
| `HUDP_STATE_CONNECTING` | client | sending `CONN_REQ`, waiting for `CONN_ACK`      |
| `HUDP_STATE_CONNECTED`  | both   | the session is up, `hudp_send()` works          |

The [state diagram](guide.md#how-it-works-in-30-seconds) shows which events move an endpoint between them.

## Configuration

`hudp_config_t` holds the timings, in milliseconds. Pass `NULL` instead of a config to use the defaults.

| Field                   | Default | Meaning                                                    |
|-------------------------|---------|------------------------------------------------------------|
| `handshake_interval_ms` | 250     | how often a connecting client repeats `CONN_REQ`           |
| `keepalive_interval_ms` | 100     | send silence after which an empty keep-alive goes out      |
| `link_timeout_ms`       | 1000    | receive silence after which the peer is lost               |

How to pick them: [Your Own Timings](guide.md#recipe-your-own-timings).

## Constants

| Constant            | Value | Meaning                                                    |
|---------------------|-------|------------------------------------------------------------|
| `HUDP_DEFAULT_PORT` | 5555  | the port the examples use                                  |
| `HUDP_MAX_PAYLOAD`  | 1200  | the largest payload, so a packet stays well below a 1500-byte MTU |
