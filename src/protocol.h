#ifndef PROTOCOL_H
#define PROTOCOL_H

// Wire format v1: [packet type: 1 byte][session id: 1 byte][payload]
#define MSG_CONN_REQ 0x01
#define MSG_CONN_ACK 0x02
#define MSG_DATA 0x03

#define HUDP_HEADER_SIZE 2

#endif
