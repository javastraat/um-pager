#pragma once
#include <Arduino.h>

#define MESH_TYPE_DATA 0x15

// Send a message to a specific destination
void um_mesh_send_message(const uint8_t *destMac, uint8_t appId, const String &payload);
// Send a message directly to the coordinator
void um_mesh_send_to_coordinator(uint8_t appId, const String &payload);
