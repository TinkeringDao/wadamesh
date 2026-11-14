// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>

struct MeshtasticRxEvent {
  uint32_t from_id;
  uint32_t to_id;
  uint32_t packet_id;
  uint32_t request_id;
  uint16_t portnum;
  uint8_t  hop_start;
  uint8_t  hop_limit;
  bool     want_ack;
  int16_t  rssi;
  float    snr;
  uint32_t rx_ms;
  uint32_t rtt_ms;
  char     kind[16];
  char     text[201];
  char     long_name[40];
  char     short_name[8];
  uint8_t  hw_model;
  uint8_t  role;
  bool     has_pos;
  int32_t  lat_i;
  int32_t  lon_i;
  int32_t  alt_m;
  uint32_t pos_time;
  uint8_t  error_reason;
  uint8_t  route_count;
  uint32_t route[8];
  uint8_t  snr_towards_count;
  int8_t   snr_towards[8];
  uint8_t  route_back_count;
  uint32_t route_back[8];
  uint8_t  snr_back_count;
  int8_t   snr_back[8];
};

struct MeshtasticRadioStatus {
  bool     active;
  bool     dual_watch;
  bool     on_meshtastic;
  uint8_t  preset;
  float    freq_mhz;
  float    bw_khz;
  uint8_t  sf;
  uint8_t  cr;
  uint32_t self_id;
  uint32_t rx_count;
  uint32_t tx_count;
  uint32_t duty_used_ms;
  int32_t  duty_remaining_ms;
  int16_t  last_rssi;
  float    last_snr;
};

// Borrow the LoRa radio for Meshtastic cross-play (preset 0..3, freq_mhz = 0 for auto region
// LongFast slot, dual_watch = true to interleave 4.0 s Meshtastic RX with 1.5 s MeshCore RX).
bool meshtasticRadioStart(uint8_t preset, float freq_mhz, bool dual_watch);

// Unconditionally restore the MeshCore radio parameters (including LR1110 STDBY_RC image
// calibration, 0x12 syncword, and preamble) and release hardware ownership. Idempotent.
void meshtasticRadioStop();

// Service incoming Meshtastic packets, TX completion, and Dual-Watch time-slicing.
void meshtasticRadioTick();

// True while the physical radio is currently tuned to the Meshtastic slice (so main.cpp skips
// the_mesh.loop()). False during the MeshCore slice of Dual-Watch or when stopped.
bool meshtasticRadioOwnsHardware();

void meshtasticRadioGetStatus(MeshtasticRadioStatus* out);
bool meshtasticRadioPopRx(MeshtasticRxEvent* out);

// Returns the 32-bit Meshtastic packet_id on success, or 0 if rate-limited / radio busy / off.
uint32_t meshtasticRadioSendText(const char* text, uint32_t to_id, bool want_ack);
uint32_t meshtasticRadioPing(uint32_t to_id, bool traceroute);
uint32_t meshtasticRadioBeacon();
