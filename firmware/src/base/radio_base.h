// TermoMade base — ESP-NOW server on the soft-AP interface.
//  * node registry (MAC, role) persisted in NVS "tmnodes"
//  * pairing window (60 s): HELLO from an unknown MAC -> HelloAck with net id + role
//  * telemetry -> RoomSample to the control task, node RTC time -> timekeeper
//  * every telemetry is answered at once with an ACK built from the latest Snapshot
#pragma once
#include <cstdint>

namespace radio_base {
bool begin(uint8_t net_id, uint8_t channel, const char* ssid, const char* pass);
void startTask();                 // radio task on core 0
void openPairing();               // 60 s window
bool pairingOpen();
void forgetAll();
// Link info for the UI / snapshot.
bool roomPaired();
int8_t roomRssi();
uint32_t roomAgeS();              // UINT32_MAX if never heard
}  // namespace radio_base
