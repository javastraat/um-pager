#pragma once
// Minimal UniversalMesh stub for the desktop SDL2 simulator.
// Provides only the MeshPacket struct and packet-type constants used by um_lora.cpp.
// The real library (ESP-NOW/WiFi) is not available on native/desktop builds.

#include <stdint.h>

#define MESH_TYPE_PING          0x12
#define MESH_TYPE_PONG          0x13
#define MESH_TYPE_ACK           0x14
#define MESH_TYPE_DATA          0x15
#define MESH_TYPE_KEY_REQ       0x16
#define MESH_TYPE_SECURE_DATA   0x17
#define MESH_TYPE_PARANOID_DATA 0x18

struct __attribute__((packed)) MeshPacket {
    uint8_t  type;
    uint8_t  ttl;
    uint32_t msgId;
    uint8_t  destMac[6];
    uint8_t  srcMac[6];
    uint8_t  appId;
    uint8_t  payloadLen;
    uint8_t  payload[200];
};
