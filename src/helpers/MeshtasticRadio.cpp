// SPDX-License-Identifier: GPL-3.0-or-later
#include "MeshtasticRadio.h"
#include "MeshtasticWire.h"

#include <string.h>
#include <stdio.h>
#include <math.h>

#if defined(ESP32)
  #include <Arduino.h>
  #include <esp_heap_caps.h>
  #include <esp_system.h>
#endif

#if !defined(WADA_WEB) && defined(ESP32)
  #include "../MyMesh.h"
#endif

// Provided by UITask.cpp
extern void luaHostSelfInfo(char* name, size_t name_cap, double* lat, double* lon,
                            char* pk_hex, size_t pk_cap, int32_t* lat_e6, int32_t* lon_e6);

namespace {

static constexpr size_t   kRxRingCap            = 16;
static constexpr size_t   kMaxPendingTx         = 8;
static constexpr size_t   kMaxDutySamples       = 64;
static constexpr uint32_t kMinTxGapMs           = 5000;
static constexpr uint32_t kDutyWindowMs         = 3600000UL;   // 1 hour
static constexpr uint32_t kDutyMaxAirMs         = 360000UL;    // 10% of 1 hour = 360 s
static constexpr uint32_t kMeshSliceMs          = 4000;        // 4.0 s Meshtastic listen window
static constexpr uint32_t kCoreSliceMs          = 1500;        // 1.5 s MeshCore listen window
static constexpr uint32_t kAckHoldWindowMs      = 6000;        // hold Meshtastic RX after want_ack TX
static constexpr uint8_t  kMeshCoreSyncWord     = 0x12;        // RADIOLIB_LR11X0_LORA_SYNC_WORD_PRIVATE
static constexpr uint16_t kMeshtasticPreamble   = 16;
static constexpr uint8_t  kDefaultChannelHash   = 0x08;        // computeChannelHash("LongFast", kDefaultLongFastKey, 16)

struct PendingTx {
  uint32_t packet_id;
  uint32_t sent_ms;
  uint32_t to_id;
  bool     active;
  bool     acked;
};

struct DutySample {
  uint32_t at_ms;
  uint32_t air_ms;
};

static bool                s_active             = false;
static bool                s_dual_watch         = true;
static bool                s_on_meshtastic      = false;
static uint8_t             s_preset             = 0;
static float               s_freq_mhz           = 869.525f;
static float               s_bw_khz             = 250.0f;
static uint8_t             s_sf                 = 11;
static uint8_t             s_cr                 = 5;
static uint32_t            s_self_node_id       = 0;
static uint32_t            s_rx_count           = 0;
static uint32_t            s_tx_count           = 0;
static int16_t             s_last_rssi          = 0;
static float               s_last_snr           = 0.0f;
static uint32_t            s_slice_entered_ms   = 0;
static uint32_t            s_ack_hold_until_ms  = 0;
static uint32_t            s_last_tx_ms         = 0;
static bool                s_tx_in_flight       = false;
static uint32_t            s_tx_start_ms        = 0;
static uint32_t            s_tx_timeout_ms      = 0;
static bool                s_queued_pos_beacon  = false;

static MeshtasticRxEvent*  s_rx_ring            = nullptr;
static size_t              s_rx_head            = 0;
static size_t              s_rx_tail            = 0;
static size_t              s_rx_used            = 0;

static PendingTx           s_pending[kMaxPendingTx];
static DutySample          s_duty[kMaxDutySamples];
static size_t              s_duty_count         = 0;

static void presetModemParams(uint8_t preset, float* bw_khz, uint8_t* sf, uint8_t* cr) {
  switch (preset) {
    case 1:  *bw_khz = 125.0f; *sf = 11; *cr = 8; break;  // LongModerate
    case 2:  *bw_khz = 250.0f; *sf = 9;  *cr = 5; break;  // MediumFast
    case 3:  *bw_khz = 250.0f; *sf = 7;  *cr = 5; break;  // ShortFast
    default: *bw_khz = 250.0f; *sf = 11; *cr = 5; break;  // LongFast (0)
  }
}

#if defined(ESP32)
static uint32_t nowMs() { return millis(); }
static uint32_t nextPacketId() {
  uint32_t r = esp_random() & 0x7FFFFFFFu;
  return r ? r : 1u;
}
#else
static uint32_t nowMs() { return 0; }
static uint32_t nextPacketId() { return 1u; }
#endif

static void pruneDutySamples(uint32_t now) {
  size_t write_idx = 0;
  for (size_t i = 0; i < s_duty_count; ++i) {
    if ((uint32_t)(now - s_duty[i].at_ms) < kDutyWindowMs) {
      s_duty[write_idx++] = s_duty[i];
    }
  }
  s_duty_count = write_idx;
}

static uint32_t getDutyUsedMs(uint32_t now) {
  pruneDutySamples(now);
  uint32_t sum = 0;
  for (size_t i = 0; i < s_duty_count; ++i) sum += s_duty[i].air_ms;
  return sum;
}

static void recordDutySample(uint32_t now, uint32_t air_ms) {
  pruneDutySamples(now);
  if (s_duty_count < kMaxDutySamples) {
    s_duty[s_duty_count++] = DutySample{now, air_ms};
  } else {
    memmove(&s_duty[0], &s_duty[1], (kMaxDutySamples - 1) * sizeof(DutySample));
    s_duty[kMaxDutySamples - 1] = DutySample{now, air_ms};
  }
}

static void pushRxEvent(const MeshtasticRxEvent& ev) {
  if (!s_rx_ring) return;
  if (s_rx_used == kRxRingCap) {
    s_rx_tail = (s_rx_tail + 1) % kRxRingCap;
    s_rx_used--;
  }
  s_rx_ring[s_rx_head] = ev;
  s_rx_head = (s_rx_head + 1) % kRxRingCap;
  s_rx_used++;
}

static void recordPendingTx(uint32_t packet_id, uint32_t now, uint32_t to_id) {
  size_t slot = 0;
  uint32_t oldest_age = 0;
  for (size_t i = 0; i < kMaxPendingTx; ++i) {
    if (!s_pending[i].active) {
      slot = i;
      break;
    }
    uint32_t age = now - s_pending[i].sent_ms;
    if (age >= oldest_age) {
      oldest_age = age;
      slot = i;
    }
  }
  s_pending[slot] = PendingTx{packet_id, now, to_id, true, false};
}

static uint32_t matchPendingTx(uint32_t packet_id, uint32_t now) {
  if (!packet_id) return 0;
  for (size_t i = 0; i < kMaxPendingTx; ++i) {
    if (s_pending[i].active && s_pending[i].packet_id == packet_id) {
      uint32_t rtt = (now >= s_pending[i].sent_ms) ? (now - s_pending[i].sent_ms) : 1u;
      if (rtt == 0) rtt = 1u;
      s_pending[i].acked = true;
      return rtt;
    }
  }
  return 0;
}

#if !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)

static void armHardwareRx(uint16_t preamble_len) {
  radio.setPreambleLength(preamble_len);
  radio.startReceive();
  if (!radio_driver.isInRecvMode()) {
    uint8_t scratch[256];
    for (int i = 0; i < 10 && !radio_driver.isInRecvMode(); ++i) {
      (void)radio_driver.recvRaw(scratch, sizeof(scratch));
    }
    radio.setPreambleLength(preamble_len);
    radio.startReceive();
  }
}

static void waitInFlightTxBounded() {
  if (!radio_driver.isInRecvMode()) {
    uint32_t cap = radio_driver.getEstAirtimeFor(MAX_TRANS_UNIT) * 3 / 2;
    if (cap > 3000) cap = 3000;
    const uint32_t t0 = millis();
    while (millis() - t0 < cap) {
      if (radio_driver.isSendComplete()) {
        radio_driver.onSendFinished();
        break;
      }
      if (radio_driver.isInRecvMode()) break;
      delay(5);
    }
  }
}

static void tuneToMeshtastic() {
  if (s_on_meshtastic) return;
  waitInFlightTxBounded();
  s_on_meshtastic = true;   // pause the_mesh.loop() before retuning

  radio_driver.radioAcquire();
  radio_driver.rxQueueSuspend(true);
  radio.standby();
#if defined(HAS_THINKNODE_M9)
  radio.calibrateImageRejection(s_freq_mhz - 4.0f, s_freq_mhz + 4.0f);
#endif
  radio_driver.setParams(s_freq_mhz, s_bw_khz, s_sf, s_cr);
  radio.setSyncWord(MeshtasticWire::kSyncWord);
  armHardwareRx(kMeshtasticPreamble);
  radio_driver.rxQueueSuspend(false);
  radio_driver.radioRelease();

  s_slice_entered_ms = nowMs();
}

static void pollHardwareRx();

static void restoreToMeshCore() {
  if (!s_on_meshtastic) return;
  if (s_tx_in_flight) {
    const uint32_t t0 = millis();
    while (millis() - t0 < s_tx_timeout_ms) {
      if (radio_driver.isSendComplete()) break;
      delay(5);
    }
    radio_driver.onSendFinished();
    s_tx_in_flight = false;
  }
  pollHardwareRx();

  NodePrefs* p = the_mesh.getNodePrefs();
  const float    mfreq = p ? p->freq : (float)LORA_FREQ;
  const float    mbw   = p ? p->bw   : (float)LORA_BW;
  const uint8_t  msf   = p ? p->sf   : (uint8_t)LORA_SF;
  const uint8_t  mcr   = p ? p->cr   : (uint8_t)LORA_CR;
  const uint16_t mpre  = RadioLibWrapper::preambleLengthForSF(msf);

  radio_driver.radioAcquire();
  radio_driver.rxQueueSuspend(true);
  radio.standby();
#if defined(HAS_THINKNODE_M9)
  radio.calibrateImageRejection(mfreq - 4.0f, mfreq + 4.0f);
#endif
  radio_driver.setParams(mfreq, mbw, msf, mcr);
  if (p) {
    radio_driver.setTxPower(p->tx_power_dbm);
    radio_driver.setRxBoostedGainMode(p->rx_boosted_gain != 0);
  }
  radio.setSyncWord(kMeshCoreSyncWord);
  armHardwareRx(mpre);
  radio_driver.rxQueueSuspend(false);
  radio_driver.radioRelease();

  s_on_meshtastic = false;
  s_slice_entered_ms = nowMs();
}

static void pollHardwareRx() {
  if (!s_on_meshtastic || s_tx_in_flight) return;
  uint8_t raw[256];
  for (int pass = 0; pass < 4; ++pass) {
    int len = radio_driver.recvRaw(raw, sizeof(raw));
    if (len <= 0) break;
    const float   snr  = radio_driver.getLastSNR();
    const int16_t rssi = (int16_t)lroundf(radio_driver.getLastRSSI());
    const uint32_t now = nowMs();
    s_last_rssi = rssi;
    s_last_snr  = snr;

    MeshtasticWire::Header hdr{};
    if (!MeshtasticWire::unpackHeader(raw, (size_t)len, hdr)) continue;

    // Implicit ACK: a nearby Meshtastic router rebroadcasting our own packet proves RF reachability
    if (hdr.from == s_self_node_id) {
      uint32_t rtt = matchPendingTx(hdr.id, now);
      if (rtt > 0) {
        MeshtasticRxEvent ack_ev{};
        ack_ev.from_id      = hdr.relay_node ? (uint32_t)hdr.relay_node : hdr.from;
        ack_ev.to_id        = s_self_node_id;
        ack_ev.packet_id    = hdr.id;
        ack_ev.request_id   = hdr.id;
        ack_ev.portnum      = MeshtasticWire::PORT_ROUTING;
        ack_ev.hop_start    = hdr.hop_start;
        ack_ev.hop_limit    = hdr.hop_limit;
        ack_ev.rssi         = rssi;
        ack_ev.snr          = snr;
        ack_ev.rx_ms        = now;
        ack_ev.rtt_ms       = rtt;
        ack_ev.error_reason = 0;
        snprintf(ack_ev.kind, sizeof(ack_ev.kind), "ack");
        pushRxEvent(ack_ev);
        s_ack_hold_until_ms = 0;
      }
      continue;
    }

    MeshtasticWire::DecodedPacket dec{};
    if (!MeshtasticWire::decodePacket(raw, (size_t)len, MeshtasticWire::kDefaultLongFastKey,
                                      kDefaultChannelHash, dec)) {
      continue;
    }

    s_rx_count++;
    MeshtasticRxEvent ev{};
    ev.from_id    = dec.hdr.from;
    ev.to_id      = dec.hdr.to;
    ev.packet_id  = dec.hdr.id;
    ev.request_id = dec.request_id;
    ev.portnum    = dec.port;
    ev.hop_start  = dec.hdr.hop_start;
    ev.hop_limit  = dec.hdr.hop_limit;
    ev.want_ack   = dec.hdr.want_ack;
    ev.rssi       = rssi;
    ev.snr        = snr;
    ev.rx_ms      = now;
    if (dec.request_id != 0) {
      ev.rtt_ms = matchPendingTx(dec.request_id, now);
      if (ev.rtt_ms > 0) s_ack_hold_until_ms = 0;
    }

    if (dec.port == MeshtasticWire::PORT_TEXT) {
      snprintf(ev.kind, sizeof(ev.kind), "text");
      snprintf(ev.text, sizeof(ev.text), "%s", dec.text);
      pushRxEvent(ev);
    } else if (dec.port == MeshtasticWire::PORT_POSITION) {
      snprintf(ev.kind, sizeof(ev.kind), "position");
      ev.has_pos  = dec.has_pos;
      ev.lat_i    = dec.lat_i;
      ev.lon_i    = dec.lon_i;
      ev.alt_m    = dec.alt_m;
      ev.pos_time = dec.pos_time;
      pushRxEvent(ev);
    } else if (dec.port == MeshtasticWire::PORT_NODEINFO) {
      snprintf(ev.kind, sizeof(ev.kind), "nodeinfo");
      snprintf(ev.long_name, sizeof(ev.long_name), "%s", dec.long_name);
      snprintf(ev.short_name, sizeof(ev.short_name), "%s", dec.short_name);
      ev.hw_model = (uint8_t)(dec.hw_model & 0xFFu);
      ev.role     = dec.role;
      pushRxEvent(ev);
    } else if (dec.port == MeshtasticWire::PORT_ROUTING) {
      snprintf(ev.kind, sizeof(ev.kind), "routing");
      ev.error_reason = dec.routing_err;
      pushRxEvent(ev);
    } else if (dec.port == MeshtasticWire::PORT_TRACEROUTE) {
      snprintf(ev.kind, sizeof(ev.kind), "traceroute");
      ev.route_count       = dec.route_cnt;
      ev.snr_towards_count = dec.snr_towards_cnt;
      ev.route_back_count  = dec.route_back_cnt;
      ev.snr_back_count    = dec.snr_back_cnt;
      for (uint8_t i = 0; i < dec.route_cnt && i < 8; ++i) ev.route[i] = dec.route[i];
      for (uint8_t i = 0; i < dec.snr_towards_cnt && i < 8; ++i) ev.snr_towards[i] = dec.snr_towards[i];
      for (uint8_t i = 0; i < dec.route_back_cnt && i < 8; ++i) ev.route_back[i] = dec.route_back[i];
      for (uint8_t i = 0; i < dec.snr_back_cnt && i < 8; ++i) ev.snr_back[i] = dec.snr_back[i];
      pushRxEvent(ev);
    } else {
      snprintf(ev.kind, sizeof(ev.kind), "other");
      pushRxEvent(ev);
    }
  }
}

static uint32_t transmitWireFrame(const uint8_t* wire, size_t wire_len, uint32_t pkt_id,
                                  uint32_t to_id, bool want_ack) {
  if (!s_active || !wire || wire_len == 0) return 0;
  const uint32_t now = nowMs();
  if (s_tx_in_flight) return 0;
  if (s_last_tx_ms != 0 && (uint32_t)(now - s_last_tx_ms) < kMinTxGapMs) return 0;

  if (!s_on_meshtastic) tuneToMeshtastic();

  uint32_t air_ms = radio_driver.getEstAirtimeFor((int)wire_len);
  if (air_ms < 50) air_ms = 50;
  if (air_ms > 4000) air_ms = 4000;

  const uint32_t used_ms = getDutyUsedMs(now);
  if (used_ms + air_ms > kDutyMaxAirMs) return 0;
  if (the_mesh.getRemainingTxBudget() < air_ms) return 0;

  if (!radio_driver.startSendRaw(wire, (int)wire_len)) {
    armHardwareRx(kMeshtasticPreamble);
    return 0;
  }

  s_tx_in_flight  = true;
  s_tx_start_ms   = now;
  s_tx_timeout_ms = air_ms * 2 + 500;
  if (s_tx_timeout_ms > 6000) s_tx_timeout_ms = 6000;
  s_last_tx_ms    = now;
  s_tx_count++;
  recordDutySample(now, air_ms);
  recordPendingTx(pkt_id, now, to_id);
  if (want_ack) s_ack_hold_until_ms = now + kAckHoldWindowMs;
  return pkt_id;
}

static uint32_t sendPositionPacketNow() {
  char name[36] = "", pk[12] = "";
  double lat = 0.0, lon = 0.0;
  int32_t lat6 = 0, lon6 = 0;
  luaHostSelfInfo(name, sizeof(name), &lat, &lon, pk, sizeof(pk), &lat6, &lon6);
  if (lat6 == 0 && lon6 == 0) return 0;

  const uint32_t pkt_id = nextPacketId();
  const uint32_t now_s  = the_mesh.getRTCClock() ? the_mesh.getRTCClock()->getCurrentTime() : 0;
  uint8_t wire[256];
  size_t wire_len = MeshtasticWire::encodePositionPacket(
      wire, sizeof(wire), s_self_node_id, pkt_id, kDefaultChannelHash,
      MeshtasticWire::kDefaultLongFastKey, lat6 * 10, lon6 * 10, 0, now_s);
  if (wire_len == 0) return 0;
  return transmitWireFrame(wire, wire_len, pkt_id, MeshtasticWire::kBroadcastAddr, false);
}

#endif  // !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)

}  // namespace

bool meshtasticRadioStart(uint8_t preset, float freq_mhz, bool dual_watch) {
#if !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)
  if (!s_rx_ring) {
    s_rx_ring = (MeshtasticRxEvent*)heap_caps_malloc(
        kRxRingCap * sizeof(MeshtasticRxEvent), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_rx_ring) {
      s_rx_ring = (MeshtasticRxEvent*)heap_caps_malloc(
          kRxRingCap * sizeof(MeshtasticRxEvent), MALLOC_CAP_8BIT);
    }
    if (!s_rx_ring) return false;
  }
  s_rx_head = s_rx_tail = s_rx_used = 0;
  memset(s_pending, 0, sizeof(s_pending));
  s_queued_pos_beacon = false;

  if (preset > 3) preset = 0;
  s_preset = preset;
  presetModemParams(preset, &s_bw_khz, &s_sf, &s_cr);

  if (freq_mhz >= 150.0f && freq_mhz <= 960.0f) {
    s_freq_mhz = freq_mhz;
  } else {
    NodePrefs* p = the_mesh.getNodePrefs();
    const float mf = p ? p->freq : (float)LORA_FREQ;
    if (mf >= 860.0f && mf <= 875.0f)      s_freq_mhz = 869.525f;  // EU_868 LongFast
    else if (mf >= 902.0f && mf <= 928.0f) s_freq_mhz = 906.875f;  // US_915 LongFast
    else if (mf >= 430.0f && mf <= 440.0f) s_freq_mhz = 433.875f;  // EU_433 LongFast
    else                                   s_freq_mhz = 869.525f;
  }

  s_self_node_id = MeshtasticWire::nodeIdFromPubKey(the_mesh.self_id.pub_key);
  s_dual_watch   = dual_watch;
  s_active       = true;

  if (s_on_meshtastic) {
    s_on_meshtastic = false;
  }
  tuneToMeshtastic();
  return true;
#else
  (void)preset;
  (void)freq_mhz;
  (void)dual_watch;
  return false;
#endif
}

void meshtasticRadioStop() {
#if !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)
  if (s_on_meshtastic) {
    restoreToMeshCore();
  }
  s_active            = false;
  s_tx_in_flight      = false;
  s_ack_hold_until_ms = 0;
  s_queued_pos_beacon = false;
  if (s_rx_ring) {
    heap_caps_free(s_rx_ring);
    s_rx_ring = nullptr;
  }
  s_rx_head = s_rx_tail = s_rx_used = 0;
#endif
}

void meshtasticRadioTick() {
#if !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)
  if (!s_active) return;
  const uint32_t now = nowMs();

  if (s_tx_in_flight) {
    if (radio_driver.isSendComplete() || (uint32_t)(now - s_tx_start_ms) >= s_tx_timeout_ms) {
      radio_driver.onSendFinished();
      armHardwareRx(kMeshtasticPreamble);
      s_tx_in_flight = false;
    } else {
      return;
    }
  }

  if (s_on_meshtastic) {
    pollHardwareRx();
  }

  if (s_queued_pos_beacon && !s_tx_in_flight &&
      (s_last_tx_ms == 0 || (uint32_t)(now - s_last_tx_ms) >= kMinTxGapMs)) {
    s_queued_pos_beacon = false;
    (void)sendPositionPacketNow();
    if (s_tx_in_flight) return;
  }

  if (!s_dual_watch) {
    if (!s_on_meshtastic) tuneToMeshtastic();
    return;
  }

  const uint32_t elapsed = now - s_slice_entered_ms;
  if (s_on_meshtastic) {
    if (elapsed >= kMeshSliceMs) {
      if (s_ack_hold_until_ms != 0 && (int32_t)(s_ack_hold_until_ms - now) > 0) return;
      if (radio_driver.isReceiving()) return;
      restoreToMeshCore();
    }
  } else {
    if (elapsed >= kCoreSliceMs) {
      if (radio_driver.isReceiving() || !radio_driver.isInRecvMode()) return;
      tuneToMeshtastic();
    }
  }
#endif
}

bool meshtasticRadioOwnsHardware() {
  return s_active && s_on_meshtastic;
}

void meshtasticRadioGetStatus(MeshtasticRadioStatus* out) {
  if (!out) return;
  memset(out, 0, sizeof(*out));
  const uint32_t now  = nowMs();
  const uint32_t used = getDutyUsedMs(now);
  out->active            = s_active;
  out->dual_watch        = s_dual_watch;
  out->on_meshtastic     = s_on_meshtastic;
  out->preset            = s_preset;
  out->freq_mhz          = s_freq_mhz;
  out->bw_khz            = s_bw_khz;
  out->sf                = s_sf;
  out->cr                = s_cr;
  out->self_id           = s_self_node_id;
  out->rx_count          = s_rx_count;
  out->tx_count          = s_tx_count;
  out->duty_used_ms      = used;
  out->duty_remaining_ms = (used < kDutyMaxAirMs) ? (int32_t)(kDutyMaxAirMs - used) : 0;
  out->last_rssi         = s_last_rssi;
  out->last_snr          = s_last_snr;
}

bool meshtasticRadioPopRx(MeshtasticRxEvent* out) {
  if (!out || !s_rx_ring || s_rx_used == 0) return false;
  *out = s_rx_ring[s_rx_tail];
  s_rx_tail = (s_rx_tail + 1) % kRxRingCap;
  s_rx_used--;
  return true;
}

uint32_t meshtasticRadioSendText(const char* text, uint32_t to_id, bool want_ack) {
#if !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)
  if (!s_active || !text || !*text) return 0;
  const uint32_t dst    = to_id ? to_id : MeshtasticWire::kBroadcastAddr;
  const uint32_t pkt_id = nextPacketId();
  uint8_t wire[256];
  size_t wire_len = MeshtasticWire::encodeTextPacket(
      wire, sizeof(wire), s_self_node_id, dst, pkt_id, kDefaultChannelHash,
      MeshtasticWire::kDefaultLongFastKey, text, want_ack);
  if (wire_len == 0) return 0;
  return transmitWireFrame(wire, wire_len, pkt_id, dst, want_ack);
#else
  (void)text;
  (void)to_id;
  (void)want_ack;
  return 0;
#endif
}

uint32_t meshtasticRadioPing(uint32_t to_id, bool traceroute) {
#if !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)
  if (!s_active || to_id == 0 || to_id == MeshtasticWire::kBroadcastAddr) return 0;
  const uint32_t pkt_id = nextPacketId();
  uint8_t wire[256];
  size_t wire_len = 0;
  if (traceroute) {
    wire_len = MeshtasticWire::encodeTraceroutePacket(
        wire, sizeof(wire), s_self_node_id, to_id, pkt_id, kDefaultChannelHash,
        MeshtasticWire::kDefaultLongFastKey);
  } else {
    wire_len = MeshtasticWire::encodeTextPacket(
        wire, sizeof(wire), s_self_node_id, to_id, pkt_id, kDefaultChannelHash,
        MeshtasticWire::kDefaultLongFastKey, "ping", true);
  }
  if (wire_len == 0) return 0;
  return transmitWireFrame(wire, wire_len, pkt_id, to_id, true);
#else
  (void)to_id;
  (void)traceroute;
  return 0;
#endif
}

uint32_t meshtasticRadioBeacon() {
#if !defined(WADA_WEB) && defined(ESP32) && defined(RADIO_CLASS)
  if (!s_active) return 0;
  char name[36] = "", pk[12] = "";
  double lat = 0.0, lon = 0.0;
  int32_t lat6 = 0, lon6 = 0;
  luaHostSelfInfo(name, sizeof(name), &lat, &lon, pk, sizeof(pk), &lat6, &lon6);

  char short_name[6] = "";
  size_t s_idx = 0;
  for (size_t i = 0; name[i] && s_idx < 4; ++i) {
    unsigned char c = (unsigned char)name[i];
    if (c > 0x20 && c < 0x7F) short_name[s_idx++] = (char)c;
  }
  short_name[s_idx] = '\0';
  if (s_idx == 0) {
    snprintf(short_name, sizeof(short_name), "%04X", (unsigned)(s_self_node_id & 0xFFFFu));
  }

  const uint32_t pkt_id = nextPacketId();
  char long_name[40];
  snprintf(long_name, sizeof(long_name), "%s [MC]", name[0] ? name : "WadaMesh");
  uint8_t wire[256];
  size_t wire_len = MeshtasticWire::encodeNodeInfoPacket(
      wire, sizeof(wire), s_self_node_id, pkt_id, kDefaultChannelHash,
      MeshtasticWire::kDefaultLongFastKey, long_name, short_name);
  if (wire_len == 0) return 0;

  uint32_t sent_id = transmitWireFrame(wire, wire_len, pkt_id, MeshtasticWire::kBroadcastAddr, false);
  if (sent_id != 0 && (lat6 != 0 || lon6 != 0)) {
    s_queued_pos_beacon = true;
  }
  return sent_id;
#else
  return 0;
#endif
}
