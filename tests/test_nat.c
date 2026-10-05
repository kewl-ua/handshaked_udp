// Scenario tests for libhudp behind the CGNAT emulator: the client talks to natemu,
// natemu translates and forwards to the server, like a carrier NAT would.
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#include "harness.h"
#include "natemu.h"

#define SERVER_PORT 15565
#define NAT_PORT 15566

static natemu_t *nat;

static void step(void) {
    if (nat) {
        natemu_service(nat);
    }

    drain(&client);
    drain(&server);

    if (nat) {
        natemu_service(nat);
    }

    poll(NULL, 0, 1);
}

static void free_nat(void) {
    natemu_free(nat);
    nat = NULL;
}

static natemu_config_t nat_config(uint32_t mapping_timeout_ms) {
    natemu_config_t cfg;

    natemu_config_default(&cfg);
    cfg.inside_port = NAT_PORT;
    cfg.server_port = SERVER_PORT;
    cfg.mapping_timeout_ms = mapping_timeout_ms;

    return cfg;
}

static bool open_through_nat(const natemu_config_t *ncfg, const hudp_config_t *hcfg) {
    nat = natemu_new(ncfg);
    server.h = hudp_server_new(SERVER_PORT, hcfg);
    client.h = hudp_client_new("127.0.0.1", NAT_PORT, hcfg);

    if (!nat || !server.h || !client.h) {
        perror("  creation failed");
        return false;
    }

    WAIT_FOR(client.events[HUDP_EVENT_CONNECTED] && server.events[HUDP_EVENT_CONNECTED], 2000);

    return client.events[HUDP_EVENT_CONNECTED] == 1 && server.events[HUDP_EVENT_CONNECTED] == 1;
}

static uint16_t server_peer_port(void) {
    char peer[32];

    if (hudp_peer(server.h, peer, sizeof(peer)) < 0) {
        return 0;
    }

    return (uint16_t)atoi(strrchr(peer, ':') + 1);
}

static void test_handshake_through_cgnat(void) {
    natemu_config_t ncfg = nat_config(1000);
    hudp_config_t hcfg = fast_config();

    CHECK(open_through_nat(&ncfg, &hcfg));

    // The server sees the NAT's public port, not the client's own one
    CHECK(natemu_public_port(nat) != 0);
    CHECK(server_peer_port() == natemu_public_port(nat));
}

static void test_keepalive_outlives_mapping_timeout(void) {
    natemu_config_t ncfg = nat_config(150);
    hudp_config_t hcfg = fast_config(); // Keep-alive every 30 ms

    CHECK(open_through_nat(&ncfg, &hcfg));

    PUMP(1000); // Over six mapping timeouts with no application data

    natemu_stats_t stats = natemu_stats(nat);

    CHECK(stats.mappings == 1 && stats.expired == 0);
    CHECK(client.events[HUDP_EVENT_LOST] == 0 && server.events[HUDP_EVENT_LOST] == 0);
}

static void test_slow_keepalive_recovers_by_migration(void) {
    natemu_config_t ncfg = nat_config(100);
    hudp_config_t hcfg = fast_config();

    // Misconfigured: the mapping expires between keep-alives, so every keep-alive gets a new port
    hcfg.keepalive_interval_ms = 150;
    hcfg.link_timeout_ms = 500;

    CHECK(open_through_nat(&ncfg, &hcfg));

    PUMP(1000);

    CHECK(natemu_stats(nat).expired > 0);
    CHECK(server.events[HUDP_EVENT_MIGRATED] > 0); // The server followed every new port
    CHECK(client.events[HUDP_EVENT_LOST] == 0 && server.events[HUDP_EVENT_LOST] == 0);
}

static void test_port_change_mid_session(void) {
    natemu_config_t ncfg = nat_config(1000);
    hudp_config_t hcfg = fast_config();

    CHECK(open_through_nat(&ncfg, &hcfg));

    uint16_t old_port = natemu_public_port(nat);

    natemu_rebind(nat);
    CHECK(hudp_send(client.h, "after", 5) == 0);
    WAIT_FOR(server.events[HUDP_EVENT_DATA] > 0, 500);

    CHECK(strcmp(server.data, "after") == 0);
    CHECK(server.events[HUDP_EVENT_MIGRATED] == 1);
    CHECK(natemu_public_port(nat) != old_port);
    CHECK(server_peer_port() == natemu_public_port(nat));

    CHECK(hudp_send(server.h, "reply", 5) == 0);
    WAIT_FOR(client.events[HUDP_EVENT_DATA] > 0, 500);
    CHECK(strcmp(client.data, "reply") == 0);
}

static void test_outage_and_reconnect(void) {
    natemu_config_t ncfg = nat_config(1000);
    hudp_config_t hcfg = fast_config(); // Link timeout 200 ms

    CHECK(open_through_nat(&ncfg, &hcfg));

    natemu_set_link(nat, false);
    WAIT_FOR(client.events[HUDP_EVENT_LOST] && server.events[HUDP_EVENT_LOST], 1000);
    CHECK(client.events[HUDP_EVENT_LOST] == 1 && server.events[HUDP_EVENT_LOST] == 1);
    CHECK(hudp_state(client.h) == HUDP_STATE_CONNECTING);

    natemu_set_link(nat, true);
    WAIT_FOR(client.events[HUDP_EVENT_CONNECTED] == 2 && server.events[HUDP_EVENT_CONNECTED] == 2, 2000);
    CHECK(client.events[HUDP_EVENT_CONNECTED] == 2 && server.events[HUDP_EVENT_CONNECTED] == 2);
}

static void test_lossy_jittery_link(void) {
    natemu_config_t ncfg = nat_config(1000);
    hudp_config_t hcfg = fast_config();

    hcfg.link_timeout_ms = 1000;

    natemu_link_t bad = { .delay_ms = 10, .jitter_ms = 8, .loss = 0.10, .duplicate = 0.05 };

    ncfg.up = bad;
    ncfg.down = bad;

    CHECK(open_through_nat(&ncfg, &hcfg));

    for (int i = 0; i < 200; i++) {
        hudp_send(client.h, "x", 1);
        PUMP(2);
    }

    PUMP(100); // Let the stragglers arrive

    // About 90 % make it, duplicates included; whatever the exact count, the session holds
    CHECK(server.events[HUDP_EVENT_DATA] >= 140);
    CHECK(client.events[HUDP_EVENT_LOST] == 0 && server.events[HUDP_EVENT_LOST] == 0);
}

int main(void) {
    run("handshake through CGNAT", test_handshake_through_cgnat, free_nat);
    run("keep-alive outlives the mapping timeout", test_keepalive_outlives_mapping_timeout, free_nat);
    run("slow keep-alive recovers by migration", test_slow_keepalive_recovers_by_migration, free_nat);
    run("port change mid-session", test_port_change_mid_session, free_nat);
    run("outage and reconnect", test_outage_and_reconnect, free_nat);
    run("lossy, jittery link", test_lossy_jittery_link, free_nat);

    return report();
}
