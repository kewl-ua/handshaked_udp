// Integration tests for libhudp: a client and a server in one process, talking over localhost.
// A plain UDP socket stands in for foreign senders and for the client after a NAT port change.
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "harness.h"

#define TEST_PORT 15555

// Wire format v1, spelled out here so the tests also pin the format down
#define WIRE_CONN_REQ 0x01
#define WIRE_CONN_ACK 0x02
#define WIRE_DATA 0x03

static void step(void) {
    drain(&client);
    drain(&server);
    poll(NULL, 0, 1);
}

static bool open_pair(void) {
    hudp_config_t cfg = fast_config();

    server.h = hudp_server_new(TEST_PORT, &cfg);
    client.h = hudp_client_new("127.0.0.1", TEST_PORT, &cfg);

    if (!server.h || !client.h) {
        perror("  endpoint creation failed");
        return false;
    }

    WAIT_FOR(client.events[HUDP_EVENT_CONNECTED] && server.events[HUDP_EVENT_CONNECTED], 1000);

    return client.events[HUDP_EVENT_CONNECTED] == 1 && server.events[HUDP_EVENT_CONNECTED] == 1;
}

static int raw_socket(void) {
    return socket(AF_INET, SOCK_DGRAM, 0);
}

static void raw_send(int fd, const void *buf, size_t len) {
    struct sockaddr_in to;

    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_port = htons(TEST_PORT);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    sendto(fd, buf, len, 0, (struct sockaddr*)&to, sizeof(to));
}

// Receives the next datagram with a payload, skipping keep-alives. Returns its length or -1.
static ssize_t raw_recv(int fd, uint8_t *buf, size_t cap, int ms) {
    uint64_t until = now_ms() + ms;

    while (now_ms() < until) {
        drain(&server);

        ssize_t n = recv(fd, buf, cap, MSG_DONTWAIT);

        if (n > 2) {
            return n;
        }

        poll(NULL, 0, 1);
    }

    return -1;
}

static void test_handshake(void) {
    char peer[32];

    CHECK(open_pair());
    CHECK(hudp_state(client.h) == HUDP_STATE_CONNECTED);
    CHECK(hudp_state(server.h) == HUDP_STATE_CONNECTED);
    CHECK(client.session_id != 0 && client.session_id == server.session_id);
    CHECK(hudp_peer(client.h, peer, sizeof(peer)) == 0 && strcmp(peer, "127.0.0.1:15555") == 0);
}

static void test_data_both_ways(void) {
    CHECK(open_pair());

    CHECK(hudp_send(client.h, "ping", 4) == 0);
    WAIT_FOR(server.events[HUDP_EVENT_DATA] > 0, 500);
    CHECK(strcmp(server.data, "ping") == 0);

    CHECK(hudp_send(server.h, "pong", 4) == 0);
    WAIT_FOR(client.events[HUDP_EVENT_DATA] > 0, 500);
    CHECK(strcmp(client.data, "pong") == 0);
}

static void test_keepalive_holds_idle_link(void) {
    CHECK(open_pair());

    PUMP(600); // Three link timeouts without any application data

    CHECK(client.events[HUDP_EVENT_LOST] == 0 && server.events[HUDP_EVENT_LOST] == 0);
    CHECK(client.events[HUDP_EVENT_DATA] == 0 && server.events[HUDP_EVENT_DATA] == 0); // Keep-alives are not delivered
}

static void test_server_answers_keepalive(void) {
    hudp_config_t cfg = fast_config();
    uint8_t req[2] = { WIRE_CONN_REQ, 0x2A };
    uint8_t keepalive[2] = { WIRE_DATA, 0x2A };
    uint8_t buf[16];

    cfg.keepalive_interval_ms = 1000; // So the server's own keep-alive can't be mistaken for the answer
    server.h = hudp_server_new(TEST_PORT, &cfg);
    CHECK(server.h != NULL);

    // A hand-made client: handshake, then one keep-alive
    int raw = raw_socket();

    raw_send(raw, req, sizeof(req));
    WAIT_FOR(server.events[HUDP_EVENT_CONNECTED] > 0, 500);
    CHECK(recv(raw, buf, sizeof(buf), MSG_DONTWAIT) == 2 && buf[0] == WIRE_CONN_ACK);

    raw_send(raw, keepalive, sizeof(keepalive));
    PUMP(50);

    // Answered at once, while the client's NAT mapping is freshly open
    CHECK(recv(raw, buf, sizeof(buf), MSG_DONTWAIT) == 2 && buf[0] == WIRE_DATA && buf[1] == 0x2A);

    close(raw);
}

static void test_foreign_conn_req_is_ignored(void) {
    CHECK(open_pair());

    int raw = raw_socket();
    uint8_t req[2] = { WIRE_CONN_REQ, (uint8_t)(client.session_id % 255 + 1) };
    uint8_t buf[16];

    raw_send(raw, req, sizeof(req));
    PUMP(100);

    CHECK(server.events[HUDP_EVENT_CONNECTED] == 1); // The session was not taken over
    CHECK(recv(raw, buf, sizeof(buf), MSG_DONTWAIT) < 0); // And the intruder got no CONN_ACK

    CHECK(hudp_send(client.h, "still mine", 10) == 0);
    WAIT_FOR(server.events[HUDP_EVENT_DATA] > 0, 500);
    CHECK(strcmp(server.data, "still mine") == 0);

    close(raw);
}

static void test_migration_to_new_port(void) {
    CHECK(open_pair());

    uint8_t session = client.session_id;

    // The NAT drops the old mapping: the client's next packet comes from another port
    hudp_free(client.h);
    client.h = NULL;

    int raw = raw_socket();
    uint8_t moved[6] = { WIRE_DATA, session, 'm', 'o', 'v', 'e' };
    uint8_t buf[16];

    raw_send(raw, moved, sizeof(moved));
    WAIT_FOR(server.events[HUDP_EVENT_MIGRATED] > 0 && server.events[HUDP_EVENT_DATA] > 0, 500);

    CHECK(server.events[HUDP_EVENT_MIGRATED] == 1);
    CHECK(strcmp(server.data, "move") == 0);
    CHECK(hudp_state(server.h) == HUDP_STATE_CONNECTED);

    // Replies follow the client to the new port
    CHECK(hudp_send(server.h, "here", 4) == 0);

    ssize_t n = raw_recv(raw, buf, sizeof(buf), 500);

    CHECK(n == 6 && buf[0] == WIRE_DATA && buf[1] == session && memcmp(buf + 2, "here", 4) == 0);

    close(raw);
}

static void test_client_reconnects_after_loss(void) {
    CHECK(open_pair());

    uint8_t first_session = client.session_id;
    hudp_config_t cfg = fast_config();

    hudp_free(server.h);
    server.h = NULL;

    WAIT_FOR(client.events[HUDP_EVENT_LOST] > 0, 1000);
    CHECK(client.events[HUDP_EVENT_LOST] == 1);
    CHECK(hudp_state(client.h) == HUDP_STATE_CONNECTING);

    server.h = hudp_server_new(TEST_PORT, &cfg);
    CHECK(server.h != NULL);

    WAIT_FOR(client.events[HUDP_EVENT_CONNECTED] == 2, 1000);
    CHECK(client.events[HUDP_EVENT_CONNECTED] == 2);
    CHECK(client.session_id != first_session); // A new session, stale packets can't mix in
}

static void test_server_accepts_new_client_after_loss(void) {
    CHECK(open_pair());

    hudp_config_t cfg = fast_config();

    hudp_free(client.h);
    client.h = NULL;

    WAIT_FOR(server.events[HUDP_EVENT_LOST] > 0, 1000);
    CHECK(server.events[HUDP_EVENT_LOST] == 1);
    CHECK(hudp_state(server.h) == HUDP_STATE_LISTENING);

    client.h = hudp_client_new("127.0.0.1", TEST_PORT, &cfg);
    CHECK(client.h != NULL);

    WAIT_FOR(server.events[HUDP_EVENT_CONNECTED] == 2, 1000);
    CHECK(server.events[HUDP_EVENT_CONNECTED] == 2);
}

static void test_send_errors(void) {
    static uint8_t oversized[HUDP_MAX_PAYLOAD + 1];
    hudp_config_t cfg = fast_config();

    CHECK(hudp_client_new("999.1.1.1", TEST_PORT, &cfg) == NULL && errno == EINVAL);

    client.h = hudp_client_new("127.0.0.1", TEST_PORT, &cfg);
    CHECK(client.h != NULL);
    CHECK(hudp_send(client.h, "x", 1) == -1 && errno == ENOTCONN);

    hudp_free(client.h);
    client.h = NULL;

    CHECK(open_pair());
    CHECK(hudp_send(client.h, oversized, sizeof(oversized)) == -1 && errno == EMSGSIZE);
    CHECK(hudp_send(client.h, oversized, HUDP_MAX_PAYLOAD) == 0);
}

int main(void) {
    run("handshake", test_handshake, NULL);
    run("data both ways", test_data_both_ways, NULL);
    run("keep-alive holds an idle link", test_keepalive_holds_idle_link, NULL);
    run("server answers a keep-alive", test_server_answers_keepalive, NULL);
    run("foreign CONN_REQ is ignored", test_foreign_conn_req_is_ignored, NULL);
    run("migration to a new port", test_migration_to_new_port, NULL);
    run("client reconnects after loss", test_client_reconnects_after_loss, NULL);
    run("server accepts a new client after loss", test_server_accepts_new_client_after_loss, NULL);
    run("send errors", test_send_errors, NULL);

    return report();
}
