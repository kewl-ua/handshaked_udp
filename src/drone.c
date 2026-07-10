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

#define MSG_CONN_REQ 0x01
#define MSG_CONN_ACK 0x02
#define MSG_DATA 0x03
#define PORT 5555
#define GND_IP "10.255.0.2" // GND public IP

int main() {
    struct timeval tv;

    srand(time(NULL));
    uint8_t session_id = (rand() % 254) + 1; // Session ID [1; 255]
    
    int sock = socket(AF_INET, SOCK_DGRAM, 0);

    if (sock < 0) {
        perror("Socker creation failed."); 
        return 1;
    }

    struct sockaddr_in gnd_addr;

    memset(&gnd_addr, 0, sizeof(gnd_addr));
    
    gnd_addr.sin_family = AF_INET;
    gnd_addr.sin_port = htons(PORT);
    inet_pton(AF_INET, GND_IP, &gnd_addr.sin_addr);

    // 200 ms timeout for Handshake stage
    tv.tv_sec = 0;
    tv.tv_usec = 200000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    printf("[DRONE] Knocking the ground side %s... Session %d\n", GND_IP, session_id);

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
            (struct sockaddr*)&gnd_addr,
            sizeof(gnd_addr)
        );

        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        ssize_t res = recvfrom(
            sock,
            rx_buffer,
            sizeof(rx_buffer),
            0,
            (struct sockaddr*)&gnd_addr,
            &from_len
        );

        if (res >= 2 && rx_buffer[0] == MSG_CONN_ACK && rx_buffer[1] == session_id) {
            connected = true; 
            printf("[DRONE] Ground responded! Connection established.\n");
        } else {
            printf("[DRONE] No response, retry in 500 ms...\n");
            usleep(500000);
        }
    }

    // 2. CRSF
    // Strict 5 ms timeout  
    tv.tv_usec = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    tx_buffer[0] = MSG_DATA;
    tx_buffer[1] = session_id;

    size_t crsf_packet_size = 2 + 26; // Heads + 26 bytes for CRSF frame

    while (true) {
        // Filling tx_buffer[2...] with frech telemetry from FC UART
        // ...

        // Sending every 4 sec for keeping alive Starlink CGNAT port reservation
        sendto(
            sock,
            tx_buffer,
            crsf_packet_size,
            0,
            (struct sockaddr*)&gnd_addr,
            sizeof(gnd_addr)
        );

        ssize_t rx_bytes = recv(sock, rx_buffer, sizeof(rx_buffer), 0);

        if (rx_bytes >= 2 && rx_buffer[0] == MSG_DATA && rx_buffer[1] == session_id) {
            // Success, rx_buffer[2...] contains CRSF frame from Ground
            // Forwarding it to FC UART
            // ...
        }

        // 4 ms cycle step for matching 250 Hz rate (CRSF 400k baud rate)
        usleep(4000);
    }

    close(sock);

    return 0;
}

