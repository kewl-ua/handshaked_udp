#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#include "protocol.h"

int main() {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        perror("Socket creation failed.");
        return 1;
    }

    struct sockaddr_in gnd_addr;

    memset(&gnd_addr, 0, sizeof(gnd_addr));

    gnd_addr.sin_family = AF_INET;
    gnd_addr.sin_port = htons(DEFAULT_PORT);
    gnd_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sock, (struct sockaddr*)&gnd_addr, sizeof(gnd_addr)) < 0) {
        perror("Bind failed");
        close(sock);
        return 1;
    }

    printf("[GND] Server running on port %d. Waiting for drone...\n", DEFAULT_PORT);

    struct sockaddr_in drone_addr;
    socklen_t addr_len = sizeof(drone_addr);

    uint8_t current_session_id = 0;
    bool connected = false;
    uint8_t rx_buffer[1024];
    uint8_t tx_buffer[1024];

    while (true) {
        ssize_t bytes_received = recvfrom(
            sock,
            rx_buffer,
            sizeof(rx_buffer),
            0,
            (struct sockaddr*)&drone_addr,
            &addr_len
        );

        // Packet too short. Not enough even for the headers
        if (bytes_received < 2) {
            continue;
        }

        uint8_t msg_type = rx_buffer[0];
        uint8_t msg_session = rx_buffer[1];

        // 1. Handshake case
        if (msg_type == MSG_CONN_REQ) {
            current_session_id = msg_session;
            connected = true;

            printf("[GND] Received CONN_REQ! Session: %d\n", current_session_id);

            tx_buffer[0] = MSG_CONN_ACK;
            tx_buffer[1] = current_session_id;

            sendto(sock, tx_buffer, 2, 0, (struct sockaddr*)&drone_addr, addr_len);
            continue;
        }

        // 2. CRSF packet
        if (msg_type == MSG_DATA && connected) {
            if (msg_session == current_session_id) {
                tx_buffer[0] = MSG_DATA;
                tx_buffer[1] = current_session_id;

                // Fill the buffer with CRSF data
                // ...

                size_t packet_size = 2 + 26; // Headers + CRSF payload
                sendto(sock, tx_buffer, packet_size, 0, (struct sockaddr*)&drone_addr, addr_len);
            }
        }
    }

    close(sock);

    return 0;
}



