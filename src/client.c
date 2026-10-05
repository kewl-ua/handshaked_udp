#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>

#include "protocol.h"

#define SERVER_IP "10.255.0.2" // Server public IP

int main() {
    struct timeval tv;

    srand(time(NULL));
    uint8_t session_id = (rand() % 254) + 1; // Session ID [1; 255]
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        perror("Socker creation failed."); 
        return 1;
    }

    struct sockaddr_in server_addr;

    memset(&server_addr, 0, sizeof(server_addr));
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(DEFAULT_PORT);
    inet_pton(AF_INET, SERVER_IP, &server_addr.sin_addr);

    // 200 ms timeout for Handshake stage
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("[CLIENT] Knocking the server %s... Session %d\n", SERVER_IP, session_id);

    // 1. Handshake
    bool connected = false;
    uint8_t tx_buffer[1024];
    uint8_t rx_buffer[1024];

    while (!connected) {
        tx_buffer[0] = MSG_CONN_REQ;
        tx_buffer[1] = session_id;

        sendto(
            sock,
            tx_buffer,
            2,
            0,
            (struct sockaddr*)&server_addr,
            sizeof(server_addr)
        );

        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t res = recvfrom(
            sock,
            rx_buffer,
            sizeof(rx_buffer),
            0,
            (struct sockaddr*)&server_addr,
            &from_len
        );

        if (res >= 2 && rx_buffer[0] == MSG_CONN_ACK && rx_buffer[1] == session_id) {
            connected = true; 
            printf("[CLIENT] Server responded! Connection established.\n");
        } else {
            printf("[CLIENT] No response, retry in 500 ms...\n");
            usleep(500000);
        }
    }

    // 2. Data exchange
    // Strict 5 ms timeout
    tv.tv_usec = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    tx_buffer[0] = MSG_DATA;
    tx_buffer[1] = session_id;

    size_t packet_size = 2 + 26; // Headers + 26 bytes of payload

    while (true) {
        // Filling tx_buffer[2...] with fresh payload
        // ...

        // Sending every 4 ms also keeps the NAT port mapping alive
        sendto(
            sock,
            tx_buffer,
            packet_size,
            0,
            (struct sockaddr*)&server_addr,
            sizeof(server_addr)
        );

        ssize_t rx_bytes = recv(sock, rx_buffer, sizeof(rx_buffer), 0);

        if (rx_bytes >= 2 && rx_buffer[0] == MSG_DATA && rx_buffer[1] == session_id) {
            // Success, rx_buffer[2...] contains payload from Server
            // ...
        }

        // 4 ms cycle step for matching 250 Hz rate
        usleep(4000);
    }

    close(sock);

    return 0;
}

