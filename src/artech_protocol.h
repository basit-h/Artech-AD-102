#ifndef ARTECH_PROTOCOL_H
#define ARTECH_PROTOCOL_H

#include <stdint.h>

enum {
  ARTECH_PACKET_STOP = 1,
  ARTECH_PACKET_READY = 2,
  ARTECH_PACKET_CALL = 3,
  ARTECH_PACKET_ERROR = 4,
  ARTECH_PHONE_NUMBER_CAPACITY = 128
};

typedef struct ArtechPacket {
  int32_t type;
  int32_t code;
  int32_t line;
  char phone_number[ARTECH_PHONE_NUMBER_CAPACITY];
} ArtechPacket;

#endif
