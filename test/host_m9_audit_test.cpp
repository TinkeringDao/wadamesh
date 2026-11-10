// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host unit tests for the ThinkNode M9 field-audit fixes:
//   1. DiscoverUpsert & onControlDataRecv tag-agnostic NODE_DISCOVER_RESP acceptance
//      + passive ADVERT path_len tracking and 0-hop reverse-SNR preservation.
//   2. Dual-scope alternating flood retry (ROUTE_TYPE_FLOOD <-> ROUTE_TYPE_TRANSPORT_FLOOD)
//      preserving calculatePacketHash() and retry-slot echo/queue matching.
//   3. Split-line serial CLI rescue command assembly (cli_command state preservation
//      across USB serial chunks starting with spaces, slashes, digits, or flags).
//
// Run from repo root:
//   c++ -std=c++17 -Wall -Wextra -I src test/host_m9_audit_test.cpp -o /tmp/host_m9_audit_test \
//       && /tmp/host_m9_audit_test

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

uint32_t g_now_ms = 1000;
uint32_t millis() { return g_now_ms; }

// ============================================================================
// 1. DiscoverUpsert & tag-agnostic onControlDataRecv + passive ADVERT path_len
// ============================================================================

constexpr uint8_t CTL_TYPE_NODE_DISCOVER_RESP = 0x90;
constexpr uint8_t ADV_TYPE_REPEATER           = 2;
constexpr uint8_t ADV_TYPE_ROOM               = 3;

struct DiscoverHit {
  uint8_t  pubkey[32];
  uint8_t  node_type;
  int8_t   our_snr_q4;
  int8_t   our_rssi;
  int8_t   their_snr_q4;
  uint8_t  path_len;
  uint16_t heard;
  uint32_t first_ms;
  uint32_t last_ms;
};

struct DiscoverModel {
  static constexpr int DISCOVER_MAX = 32;
  DiscoverHit _discover[DISCOVER_MAX]{};
  uint8_t     _discover_cnt = 0;
  uint32_t    _discover_tag = 0;
  uint32_t    _ui_sig_probe_tag = 0;
  int8_t      _ui_sig_snr_q4 = 0;
  int8_t      _ui_sig_rssi = 0;
  uint32_t    _ui_sig_ms = 0;

  void discoverUpsert(const uint8_t* pk, uint8_t pklen, uint8_t node_type,
                      int8_t our_snr_q4, int8_t our_rssi, int8_t their_snr_q4,
                      uint8_t path_len = 0) {
    if (!pk || pklen < 8) return;
    uint32_t now = millis();
    int slot = -1;
    for (uint8_t i = 0; i < _discover_cnt; i++) {
      if (std::memcmp(_discover[i].pubkey, pk, 8) == 0) { slot = i; break; }
    }
    const bool existed = (slot >= 0);
    if (slot < 0) {
      if (_discover_cnt < DISCOVER_MAX) {
        slot = _discover_cnt++;
      } else {
        uint32_t oldest = 0xFFFFFFFFu;
        slot = 0;
        for (uint8_t i = 0; i < _discover_cnt; i++) {
          if (_discover[i].last_ms < oldest) { oldest = _discover[i].last_ms; slot = i; }
        }
      }
      std::memset(&_discover[slot], 0, sizeof(DiscoverHit));
      std::memcpy(_discover[slot].pubkey, pk, pklen > 32 ? 32 : pklen);
      _discover[slot].first_ms = now;
    } else if (pklen >= 32) {
      std::memcpy(_discover[slot].pubkey, pk, 32);
    }
    DiscoverHit& h = _discover[slot];
    const bool keep_their_snr = existed && h.their_snr_q4 != 0 && (h.path_len & 0x3F) == 0 &&
                                their_snr_q4 == 0 && (uint32_t)(now - h.last_ms) < 60000UL;
    if (!keep_their_snr) {
      h.their_snr_q4 = their_snr_q4;
    }
    h.node_type  = node_type;
    h.our_snr_q4 = our_snr_q4;
    h.our_rssi   = our_rssi;
    h.path_len   = path_len;
    h.last_ms    = now;
    if (h.heard < 0xFFFF) h.heard++;
  }

  void onControlDataRecv(const uint8_t* payload, uint8_t payload_len,
                         uint8_t path_len, float rx_snr_db, int rx_rssi_dbm) {
    if (payload_len >= 6 && (payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP
        && _ui_sig_probe_tag != 0) {
      uint32_t rtag;
      std::memcpy(&rtag, &payload[2], 4);
      if (rtag == _ui_sig_probe_tag) {
        _ui_sig_snr_q4 = (int8_t)(rx_snr_db * 4.0f);
        _ui_sig_rssi   = (int8_t)rx_rssi_dbm;
        _ui_sig_ms     = millis();
      }
    }
    // Tag-agnostic NODE_DISCOVER_RESP acceptance into _discover[]:
    if (payload_len >= 6 + 8 && (payload[0] & 0xF0) == CTL_TYPE_NODE_DISCOVER_RESP) {
      uint8_t node_type    = payload[0] & 0x0F;
      int8_t  their_snr_q4 = (int8_t)payload[1];
      uint8_t pklen        = (payload_len >= 6 + 32) ? 32 : 8;
      int8_t  our_snr_q4   = (int8_t)(rx_snr_db * 4.0f);
      int8_t  our_rssi     = (int8_t)rx_rssi_dbm;
      discoverUpsert(&payload[6], pklen, node_type, our_snr_q4, our_rssi, their_snr_q4, path_len);
    }
  }
};

void testDiscoverUpsertAndControlRecv() {
  DiscoverModel m;
  m._discover_tag = 0x11111111u;
  m._ui_sig_probe_tag = 0x22222222u;
  g_now_ms = 5000;

  // 1) A NODE_DISCOVER_RESP answering _ui_sig_probe_tag (or an older wardrive sweep tag
  //    != _discover_tag) must still be recorded in _discover[].
  uint8_t resp[6 + 8]{};
  resp[0] = (uint8_t)(CTL_TYPE_NODE_DISCOVER_RESP | ADV_TYPE_REPEATER);
  resp[1] = (uint8_t)(int8_t)(-12);  // -3.0 dB reverse SNR
  uint32_t probe_tag = 0x22222222u;
  std::memcpy(&resp[2], &probe_tag, 4);
  const uint8_t pk_prefix[8] = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04};
  std::memcpy(&resp[6], pk_prefix, 8);

  m.onControlDataRecv(resp, sizeof(resp), /*path_len=*/0, /*rx_snr_db=*/6.5f, /*rx_rssi_dbm=*/-78);
  assert(m._ui_sig_ms == 5000);
  assert(m._discover_cnt == 1);
  assert(m._discover[0].node_type == ADV_TYPE_REPEATER);
  assert(m._discover[0].our_snr_q4 == 26);
  assert(m._discover[0].our_rssi == -78);
  assert(m._discover[0].their_snr_q4 == -12);
  assert(m._discover[0].path_len == 0);
  assert(m._discover[0].heard == 1);

  // 2) Even when _discover_tag == 0 (Discover UI not open yet), a reply to a background
  //    or third-party NODE_DISCOVER_REQ is captured in _discover[].
  m._discover_tag = 0;
  uint32_t foreign_tag = 0x99887766u;
  std::memcpy(&resp[2], &foreign_tag, 4);
  const uint8_t pk2[8] = {0xCA, 0xFE, 0xBA, 0xBE, 0x10, 0x20, 0x30, 0x40};
  std::memcpy(&resp[6], pk2, 8);
  g_now_ms = 6000;
  m.onControlDataRecv(resp, sizeof(resp), /*path_len=*/0, /*rx_snr_db=*/-2.0f, /*rx_rssi_dbm=*/-95);
  assert(m._discover_cnt == 2);
  assert(std::memcmp(m._discover[1].pubkey, pk2, 8) == 0);
  assert(m._discover[1].path_len == 0);

  // 3) Passive ADVERT from the first repeater 10s later (with full 32-byte pubkey and
  //    their_snr_q4 == 0, 0-hop) upgrades the 32-byte pubkey AND preserves the recent
  //    0-hop reverse SNR (-12 q4) because < 60s elapsed.
  uint8_t full_pk1[32]{};
  std::memcpy(full_pk1, pk_prefix, 8);
  for (int i = 8; i < 32; ++i) full_pk1[i] = (uint8_t)(0xA0 + i);
  g_now_ms = 15000;
  m.discoverUpsert(full_pk1, 32, ADV_TYPE_REPEATER, /*our_snr_q4=*/28, /*our_rssi=*/-76,
                   /*their_snr_q4=*/0, /*path_len=*/0);
  assert(m._discover_cnt == 2);
  assert(std::memcmp(m._discover[0].pubkey, full_pk1, 32) == 0);
  assert(m._discover[0].their_snr_q4 == -12);  // preserved!
  assert(m._discover[0].our_snr_q4 == 28);
  assert(m._discover[0].path_len == 0);
  assert(m._discover[0].heard == 2);

  // 4) Passive multi-hop ADVERT (path_len = 3, their_snr_q4 = 0) from a new repeater
  //    records path_len = 3 and their_snr_q4 = 0.
  uint8_t full_pk3[32]{};
  for (int i = 0; i < 32; ++i) full_pk3[i] = (uint8_t)(0x30 + i);
  g_now_ms = 16000;
  m.discoverUpsert(full_pk3, 32, ADV_TYPE_ROOM, /*our_snr_q4=*/-28, /*our_rssi=*/-107,
                   /*their_snr_q4=*/0, /*path_len=*/3);
  assert(m._discover_cnt == 3);
  assert(m._discover[2].node_type == ADV_TYPE_ROOM);
  assert(m._discover[2].their_snr_q4 == 0);
  assert(m._discover[2].path_len == 3);
  assert(m._discover[2].our_snr_q4 == -28);
  assert(m._discover[2].our_rssi == -107);
}

// ============================================================================
// 2. Dual-scope alternating flood retry preserving calculatePacketHash()
// ============================================================================

constexpr uint8_t ROUTE_TYPE_TRANSPORT_FLOOD  = 0x00;
constexpr uint8_t ROUTE_TYPE_FLOOD            = 0x01;
constexpr uint8_t ROUTE_TYPE_DIRECT           = 0x02;
constexpr uint8_t ROUTE_TYPE_TRANSPORT_DIRECT = 0x03;
constexpr uint8_t PH_ROUTE_MASK               = 0x03;
constexpr uint8_t PH_TYPE_SHIFT               = 2;
constexpr uint8_t PH_TYPE_MASK                = 0x0F;
constexpr uint8_t PAYLOAD_TYPE_TXT_MSG        = 0x02;
constexpr uint8_t PAYLOAD_TYPE_GRP_TXT        = 0x05;
constexpr uint8_t PAYLOAD_TYPE_TRACE          = 0x09;
constexpr size_t  MAX_HASH_SIZE               = 8;
constexpr size_t  MAX_PACKET_PAYLOAD          = 184;
constexpr size_t  MAX_PATH_SIZE               = 64;

struct TestPacket {
  uint8_t  header = 0;
  uint16_t transport_codes[2]{0, 0};
  uint8_t  path_len = 0;
  uint8_t  path[MAX_PATH_SIZE]{};
  uint8_t  payload_len = 0;
  uint8_t  payload[MAX_PACKET_PAYLOAD]{};

  uint8_t getRouteType() const { return header & PH_ROUTE_MASK; }
  uint8_t getPayloadType() const { return (header >> PH_TYPE_SHIFT) & PH_TYPE_MASK; }
  bool isRouteFlood() const {
    uint8_t rt = getRouteType();
    return rt == ROUTE_TYPE_FLOOD || rt == ROUTE_TYPE_TRANSPORT_FLOOD;
  }
  bool hasTransportCodes() const {
    uint8_t rt = getRouteType();
    return rt == ROUTE_TYPE_TRANSPORT_FLOOD || rt == ROUTE_TYPE_TRANSPORT_DIRECT;
  }

  // Exact replica of mesh::Packet::calculatePacketHash structure (hashes payload_type
  // [+ path_len if TRACE] + payload bytes; route_type and transport_codes are excluded).
  void calculatePacketHash(uint8_t out_hash[MAX_HASH_SIZE]) const {
    uint64_t h = 14695981039346656037ULL;
    auto mix = [&](uint8_t b) {
      h ^= b;
      h *= 1099511628211ULL;
    };
    uint8_t t = getPayloadType();
    mix(t);
    if (t == PAYLOAD_TYPE_TRACE) {
      mix(path_len);
    }
    for (uint8_t i = 0; i < payload_len; ++i) {
      mix(payload[i]);
    }
    std::memcpy(out_hash, &h, MAX_HASH_SIZE);
  }
};

static uint16_t calcScopeTransportCode(uint16_t scope_seed, const TestPacket* pkt) {
  uint16_t acc = scope_seed;
  acc ^= (uint16_t)(pkt->getPayloadType() << 8);
  for (uint8_t i = 0; i < pkt->payload_len; ++i) {
    acc = (uint16_t)((acc * 33u) ^ pkt->payload[i]);
  }
  return acc == 0 ? 1 : acc;
}

static TestPacket cloneWithDualScopeAlternation(const TestPacket& src, bool direct,
                                                uint16_t fallback_scope_seed) {
  TestPacket retry = src;
  if (!direct && retry.isRouteFlood()) {
    if (retry.getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD) {
      retry.header = (uint8_t)((retry.header & ~PH_ROUTE_MASK) | ROUTE_TYPE_FLOOD);
      retry.transport_codes[0] = 0;
      retry.transport_codes[1] = 0;
    } else if (retry.getRouteType() == ROUTE_TYPE_FLOOD) {
      if (fallback_scope_seed != 0) {
        retry.header = (uint8_t)((retry.header & ~PH_ROUTE_MASK) | ROUTE_TYPE_TRANSPORT_FLOOD);
        retry.transport_codes[0] = calcScopeTransportCode(fallback_scope_seed, &retry);
        retry.transport_codes[1] = 0;
      }
    }
  }
  return retry;
}

void testDualScopeFloodRetry() {
  // Initial packet sent as scoped ROUTE_TYPE_TRANSPORT_FLOOD (#region-sub)
  TestPacket orig;
  orig.header = (uint8_t)((PAYLOAD_TYPE_GRP_TXT << PH_TYPE_SHIFT) | ROUTE_TYPE_TRANSPORT_FLOOD);
  orig.payload_len = 24;
  for (uint8_t i = 0; i < orig.payload_len; ++i) orig.payload[i] = (uint8_t)(0x40 + i);
  const uint16_t scope_seed = 0xDE89;
  orig.transport_codes[0] = calcScopeTransportCode(scope_seed, &orig);
  orig.transport_codes[1] = 0;

  uint8_t orig_hash[MAX_HASH_SIZE]{};
  orig.calculatePacketHash(orig_hash);

  // Clone 1: alternates from ROUTE_TYPE_TRANSPORT_FLOOD -> unscoped ROUTE_TYPE_FLOOD
  TestPacket clone1 = cloneWithDualScopeAlternation(orig, /*direct=*/false, scope_seed);
  assert(clone1.getRouteType() == ROUTE_TYPE_FLOOD);
  assert(!clone1.hasTransportCodes());
  assert(clone1.transport_codes[0] == 0 && clone1.transport_codes[1] == 0);
  assert(clone1.getPayloadType() == PAYLOAD_TYPE_GRP_TXT);
  assert(clone1.payload_len == orig.payload_len);
  assert(std::memcmp(clone1.payload, orig.payload, orig.payload_len) == 0);

  uint8_t clone1_hash[MAX_HASH_SIZE]{};
  clone1.calculatePacketHash(clone1_hash);
  assert(std::memcmp(orig_hash, clone1_hash, MAX_HASH_SIZE) == 0);

  // Clone 2: alternates from unscoped ROUTE_TYPE_FLOOD -> scoped ROUTE_TYPE_TRANSPORT_FLOOD
  TestPacket clone2 = cloneWithDualScopeAlternation(clone1, /*direct=*/false, scope_seed);
  assert(clone2.getRouteType() == ROUTE_TYPE_TRANSPORT_FLOOD);
  assert(clone2.hasTransportCodes());
  assert(clone2.transport_codes[0] == orig.transport_codes[0]);
  assert(clone2.transport_codes[1] == 0);

  uint8_t clone2_hash[MAX_HASH_SIZE]{};
  clone2.calculatePacketHash(clone2_hash);
  assert(std::memcmp(orig_hash, clone2_hash, MAX_HASH_SIZE) == 0);

  // Direct packets must NOT be altered by flood alternation
  TestPacket direct_pkt = orig;
  direct_pkt.header = (uint8_t)((PAYLOAD_TYPE_TXT_MSG << PH_TYPE_SHIFT) | ROUTE_TYPE_DIRECT);
  direct_pkt.transport_codes[0] = 0;
  TestPacket direct_clone = cloneWithDualScopeAlternation(direct_pkt, /*direct=*/true, scope_seed);
  assert(direct_clone.getRouteType() == ROUTE_TYPE_DIRECT);
}

// ============================================================================
// 3. Split-line serial CLI rescue command assembly
// ============================================================================

struct SerialCliHarness {
  std::deque<char> rx;
  char cli_command[80]{};
  std::vector<std::string> executed_commands;
  int companion_frames_seen = 0;

  void feedBytes(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) rx.push_back(s[i]);
  }
  void feedStr(const char* s) { feedBytes(s, std::strlen(s)); }

  bool available() const { return !rx.empty(); }
  int peek() const { return rx.empty() ? -1 : (unsigned char)rx.front(); }
  char read() {
    char c = rx.front();
    rx.pop_front();
    return c;
  }

  void checkCLIRescueCmd() {
    size_t len = std::strlen(cli_command);
    bool line_complete = false;
    while (available() && len < sizeof(cli_command) - 1) {
      if (peek() == '<') {
        cli_command[0] = 0;
        return;
      }
      char c = read();
      if (c == '\r' || c == '\n') {
        line_complete = true;
        break;
      } else {
        cli_command[len++] = c;
        cli_command[len] = 0;
      }
    }
    if (len == sizeof(cli_command) - 1) {
      line_complete = true;
    }
    if (line_complete && len > 0) {
      executed_commands.emplace_back(cli_command);
      cli_command[0] = 0;
    }
  }

  void checkSerialInterface() {
    if (available() && peek() == '<') {
      companion_frames_seen++;
      // Drain mock companion frame up to '>'
      while (available()) {
        if (read() == '>') break;
      }
    }
  }

  void loopOnce() {
    while (available()) {
      if (cli_command[0] != '\0') {
        if (peek() == '<') {
          cli_command[0] = 0;
          break;
        }
        checkCLIRescueCmd();
        continue;
      }
      int first = peek();
      if (first == '\r' || first == '\n') {
        read();
        continue;
      }
      if (first == '<') {
        break;
      } else if ((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z')) {
        checkCLIRescueCmd();
      } else {
        break;
      }
    }
    checkSerialInterface();
  }
};

void testSplitLineSerialCliAssembly() {
  SerialCliHarness h;

  // Case A: "cat" in chunk 1, " /ads/diag.log\r\n" in chunk 2 (starts with space + slash)
  h.feedStr("cat");
  h.loopOnce();
  assert(h.executed_commands.empty());
  assert(std::strcmp(h.cli_command, "cat") == 0);

  h.feedStr(" /ads/diag.log\r\n");
  h.loopOnce();
  assert(h.executed_commands.size() == 1);
  assert(h.executed_commands[0] == "cat /ads/diag.log");
  assert(h.cli_command[0] == '\0');

  // Case B: "ls " in chunk 1, "/apps/wardrive\n" in chunk 2 (starts with '/')
  h.feedStr("ls ");
  h.loopOnce();
  assert(h.executed_commands.size() == 1);
  h.feedStr("/apps/wardrive\n");
  h.loopOnce();
  assert(h.executed_commands.size() == 2);
  assert(h.executed_commands[1] == "ls /apps/wardrive");

  // Case C: "set pin " in chunk 1, "123456\r\n" in chunk 2 (starts with digit '1')
  h.feedStr("set pin ");
  h.loopOnce();
  h.feedStr("123456\r\n");
  h.loopOnce();
  assert(h.executed_commands.size() == 3);
  assert(h.executed_commands[2] == "set pin 123456");

  // Case D: Multiple commands in a single burst with CRLF
  h.feedStr("wifi.status\r\nget mqtt.status\r\n");
  h.loopOnce();
  assert(h.executed_commands.size() == 5);
  assert(h.executed_commands[3] == "wifi.status");
  assert(h.executed_commands[4] == "get mqtt.status");

  // Case E: Partial CLI command interrupted by '<' companion frame resets cli_command
  //         and hands the '<' frame to checkSerialInterface().
  h.feedStr("cat ");
  h.loopOnce();
  assert(std::strcmp(h.cli_command, "cat ") == 0);
  h.feedStr("<COMPANION>");
  h.loopOnce();
  assert(h.cli_command[0] == '\0');
  assert(h.companion_frames_seen == 1);
}

struct UsbFrameTrackerModel {
  uint8_t  state = 0;
  uint16_t frame_len = 0;
  uint16_t rx_len = 0;
  uint32_t last_byte_ms = 0;

  void reset() { state = 0; frame_len = 0; rx_len = 0; last_byte_ms = 0; }
  bool inProgress() const {
    return state != 0 && (uint32_t)(millis() - last_byte_ms) <= 1000u;
  }
  bool expireIfStale() {
    if (state != 0 && (uint32_t)(millis() - last_byte_ms) > 1000u) {
      reset();
      return true;
    }
    return false;
  }
  void onByte(uint8_t c) {
    last_byte_ms = millis();
    switch (state) {
      case 0:
        if (c == '<') state = 1;
        break;
      case 1:
        frame_len = c;
        state = 2;
        break;
      case 2:
        frame_len |= (uint16_t)(((uint16_t)c) << 8);
        rx_len = 0;
        state = (frame_len > 0) ? 3 : 0;
        break;
      default:
        rx_len++;
        if (rx_len >= frame_len) state = 0;
        break;
    }
  }
};

void testUsbCompanionSplitFrameGuard() {
  UsbFrameTrackerModel tracker;
  g_now_ms = 1000;
  assert(!tracker.inProgress());

  // 5-byte companion frame split across two USB packets: chunk 1 has '<', len=5, 2 payload bytes;
  // chunk 2 starts with 'a' (which would otherwise look like a CLI command letter!).
  const uint8_t chunk1[] = {'<', 0x05, 0x00, 0x02, 'x'};
  for (uint8_t b : chunk1) tracker.onByte(b);
  assert(tracker.inProgress());

  g_now_ms = 1005;
  const uint8_t chunk2[] = {'a', 'b', 'c'};
  assert(tracker.inProgress());  // prevents MyMesh::loop() from routing 'a' to checkCLIRescueCmd
  for (uint8_t b : chunk2) tracker.onByte(b);
  assert(!tracker.inProgress()); // complete 5-byte frame resets tracker to IDLE

  // Zero-length frame ('<', 0, 0) immediately returns to IDLE
  tracker.onByte('<');
  assert(tracker.inProgress());
  tracker.onByte(0x00);
  tracker.onByte(0x00);
  assert(!tracker.inProgress());

  // Stray '<' typed on console expires after >1000 ms so interactive CLI is never wedged
  g_now_ms = 2000;
  tracker.onByte('<');
  assert(tracker.inProgress());
  g_now_ms = 3001;
  assert(!tracker.inProgress());
  assert(tracker.expireIfStale());
  assert(tracker.state == 0);
}

// ============================================================================
// 4. ClockFloorRTC mesh-peer quorum & outgoing-message floor seeding
//    + Repeater DM "ping" status request & PONG formatter
// ============================================================================

struct MockClockFloorRTC {
  enum class ClockSource : uint8_t { Seed = 0, Floor = 1, HardwareRTC = 2, ExternalSync = 3 };
  static constexpr uint32_t MIN_VALID_EPOCH            = 1772323200UL;
  static constexpr uint32_t MAX_PLAUSIBLE_EPOCH        = 1893456000UL;
  static constexpr uint32_t MAX_SINGLE_PEER_JUMP_SECS  = 3600UL;
  static constexpr uint32_t PEER_QUORUM_TOLERANCE_SECS = 900UL;

  uint32_t    _floor           = MIN_VALID_EPOCH;
  uint32_t    _pending_peer_ts = 0;
  ClockSource _source          = ClockSource::Seed;

  bool timeIsCurrent() const { return _source >= ClockSource::HardwareRTC; }
  uint32_t getCurrentTime() const { return _floor; }

  void seedFloor(uint32_t persisted) {
    if (persisted <= MIN_VALID_EPOCH || persisted > MAX_PLAUSIBLE_EPOCH) return;
    if (persisted > _floor) {
      _floor = persisted;
      if (_source < ClockSource::HardwareRTC) _source = ClockSource::Floor;
    }
  }

  void setCurrentTime(uint32_t t) {
    if (t <= MIN_VALID_EPOCH || t > MAX_PLAUSIBLE_EPOCH) return;
    if (t > _floor) _floor = t;
    _source = ClockSource::ExternalSync;
  }

  bool adoptMeshPeerTime(uint32_t peer_ts) {
    if (timeIsCurrent()) return false;
    if (peer_ts <= MIN_VALID_EPOCH || peer_ts > MAX_PLAUSIBLE_EPOCH) return false;
    const uint32_t cur = getCurrentTime();
    if (peer_ts <= cur) return false;

    const bool large_jump = (_source == ClockSource::Seed) ||
                            (cur <= MIN_VALID_EPOCH) ||
                            (peer_ts > cur + MAX_SINGLE_PEER_JUMP_SECS);
    if (large_jump) {
      if (_pending_peer_ts != 0) {
        const uint32_t diff = (peer_ts >= _pending_peer_ts)
                                  ? (peer_ts - _pending_peer_ts)
                                  : (_pending_peer_ts - peer_ts);
        if (diff <= PEER_QUORUM_TOLERANCE_SECS) {
          _pending_peer_ts = 0;
          _floor = peer_ts;
          _source = ClockSource::Floor;
          return true;
        }
      }
      _pending_peer_ts = peer_ts;
      return false;
    }

    _floor = peer_ts;
    _source = ClockSource::Floor;
    return true;
  }
};

void testClockFloorAndMeshQuorum() {
  MockClockFloorRTC rtc;
  // 1) Persisted NVS floor is stale (09:05 = 1794297900), but seg_769.bin has an outgoing
  //    message at 09:20:17 (1794298817) and a rogue incoming advert at 2026-11-13 (1794524432).
  //    Boot seeding from outgoing messages must lift floor to 09:20:17 and ignore incoming.
  rtc.seedFloor(1794297900UL);
  struct StoredMsg { bool outgoing; uint32_t ts; };
  const StoredMsg history[] = {
    {true,  1794298792UL}, // [844] outgoing ping at 09:19:52
    {true,  1794298817UL}, // [847] outgoing channel msg at 09:20:17
    {false, 1794524432UL}, // incoming message from node with future clock
  };
  uint32_t max_out_ts = 0;
  for (const auto& m : history) {
    if (m.outgoing && m.ts > MockClockFloorRTC::MIN_VALID_EPOCH &&
        m.ts <= MockClockFloorRTC::MAX_PLAUSIBLE_EPOCH && m.ts > max_out_ts) {
      max_out_ts = m.ts;
    }
  }
  rtc.seedFloor(max_out_ts);
  assert(rtc.getCurrentTime() == 1794298817UL);
  assert(!rtc.timeIsCurrent());

  // 2) Stale advert from peer A (09:05:55 = 1794297955) is rejected (< floor).
  assert(!rtc.adoptMeshPeerTime(1794297955UL));
  assert(rtc.getCurrentTime() == 1794298817UL);

  // 3) Slightly newer stale peer (09:25:00 = 1794299100, +4m43s) advances floor,
  //    but keeps !timeIsCurrent() so real-time peers at 15:08 are NOT locked out!
  assert(rtc.adoptMeshPeerTime(1794299100UL));
  assert(rtc.getCurrentTime() == 1794299100UL);
  assert(!rtc.timeIsCurrent());

  // 4) Single rogue repeater advert 2.3 days in the future (rogue future peer B
  //    2026-11-13 = 1794524432) is held for quorum and does NOT jump the floor.
  assert(!rtc.adoptMeshPeerTime(1794524432UL));
  assert(rtc.getCurrentTime() == 1794299100UL);

  // 5) Real-time peers at 15:08 (1794319680 and 1794319694, within 14s of each other)
  //    corroborate each other and advance the clock to 15:08:14!
  assert(!rtc.adoptMeshPeerTime(1794319680UL)); // first 15:08 peer replaces rogue pending
  assert(rtc.adoptMeshPeerTime(1794319694UL));  // second 15:08 peer completes quorum!
  assert(rtc.getCurrentTime() == 1794319694UL);
  assert(!rtc.timeIsCurrent()); // still Floor until GPS/NTP calls setCurrentTime()

  // 6) GPS lock at 15:09:36 (1794319776) calls setCurrentTime() -> ExternalSync.
  rtc.setCurrentTime(1794319776UL);
  assert(rtc.timeIsCurrent());
  assert(rtc.getCurrentTime() == 1794319776UL);
}

static bool isRepeaterPingCmd(const char* text) {
  if (!text) return false;
  while (*text == ' ' || *text == '\t') ++text;
  if (strncasecmp(text, "ping", 4) != 0) return false;
  text += 4;
  while (*text == ' ' || *text == '\t' || *text == '?' || *text == '!' ||
         *text == '\r' || *text == '\n') {
    ++text;
  }
  return *text == '\0';
}

static void formatRepeaterPongMessage(const uint8_t* payload, size_t payload_len,
                                      int8_t out_path_len, char* out, size_t cap) {
  if (!out || cap == 0) return;
  unsigned batt_mv = 0, uptime_s = 0, queue_len = 0;
  int16_t  noise_floor = 0, last_rssi = 0, last_snr_q4 = 0;
  bool     has_snr = false;
  bool     parsed = false;

  if (payload && payload_len >= 24) {
    batt_mv     = (unsigned)(payload[0] | (payload[1] << 8));
    queue_len   = (unsigned)(payload[2] | (payload[3] << 8));
    noise_floor = (int16_t)(payload[4] | (payload[5] << 8));
    last_rssi   = (int16_t)(payload[6] | (payload[7] << 8));
    uptime_s    = (unsigned)(payload[20] | (payload[21] << 8) |
                             (payload[22] << 16) | (payload[23] << 24));
    if (payload_len >= 44) {
      last_snr_q4 = (int16_t)(payload[42] | (payload[43] << 8));
      has_snr = true;
    }
    if (batt_mv >= 2000 && batt_mv <= 5500) parsed = true;
  }
  const int hops = (out_path_len >= 0) ? (int)(out_path_len & 0x3F) : -1;
  char hop_str[16] = "";
  if (hops >= 0) {
    std::snprintf(hop_str, sizeof(hop_str), " | %dh", hops);
  }
  if (parsed) {
    char up_str[24];
    if (uptime_s >= 3600) {
      std::snprintf(up_str, sizeof(up_str), "%uh%02um", uptime_s / 3600, (uptime_s % 3600) / 60);
    } else {
      std::snprintf(up_str, sizeof(up_str), "%um%02us", uptime_s / 60, uptime_s % 60);
    }
    if (has_snr) {
      std::snprintf(out, cap, "PONG | RX %+.1fdB %ddBm (NF %d) | %u.%02uV | up %s%s",
                    ((float)last_snr_q4) / 4.0f, (int)last_rssi, (int)noise_floor,
                    batt_mv / 1000, (batt_mv % 1000) / 10, up_str, hop_str);
    } else {
      std::snprintf(out, cap, "PONG | RX %ddBm (NF %d) | %u.%02uV | up %s | q=%u%s",
                    (int)last_rssi, (int)noise_floor,
                    batt_mv / 1000, (batt_mv % 1000) / 10, up_str, queue_len, hop_str);
    }
  } else {
    std::snprintf(out, cap, "PONG | status reply (%u bytes)%s", (unsigned)payload_len, hop_str);
  }
}

void testRepeaterDmPingAndPongFormat() {
  assert(isRepeaterPingCmd("ping"));
  assert(isRepeaterPingCmd("  PING! \n"));
  assert(isRepeaterPingCmd("Ping?"));
  assert(!isRepeaterPingCmd("ping 123"));
  assert(!isRepeaterPingCmd("neighbors"));
  assert(!isRepeaterPingCmd(""));

  uint8_t stats[56]{};
  uint16_t batt = 4048; // 4.04V
  uint16_t qlen = 0;
  int16_t  nf   = -117;
  int16_t  rssi = -76;
  uint32_t up   = 5 * 3600 + 12 * 60; // 5h12m
  int16_t  snr4 = 52; // +13.0 dB
  std::memcpy(&stats[0], &batt, 2);
  std::memcpy(&stats[2], &qlen, 2);
  std::memcpy(&stats[4], &nf, 2);
  std::memcpy(&stats[6], &rssi, 2);
  std::memcpy(&stats[20], &up, 4);
  std::memcpy(&stats[42], &snr4, 2);

  char buf[140]{};
  formatRepeaterPongMessage(stats, sizeof(stats), /*out_path_len=*/0, buf, sizeof(buf));
  assert(std::string(buf) == "PONG | RX +13.0dB -76dBm (NF -117) | 4.04V | up 5h12m | 0h");
}

// ============================================================================
// 5. MQTT opt-in consent gating, authenticated rx/dm sender parsing, and
//    outgoing DM local sender identity + direction formatting
// ============================================================================

static bool parseAuthenticatedRxDm(const char* plain, size_t len,
                                   uint8_t outSenderPrefix[6], const char** outText) {
  if (!plain || !outSenderPrefix || !outText || len < 14) return false;
  if (plain[12] != ':' || plain[13] == '\0') return false;
  bool anyNonZero = false;
  bool anyNonFF   = false;
  for (int i = 0; i < 6; ++i) {
    auto hexVal = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
      if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
      return -1;
    };
    int hi = hexVal(plain[i * 2]);
    int lo = hexVal(plain[i * 2 + 1]);
    if (hi < 0 || lo < 0) return false;
    uint8_t b = (uint8_t)((hi << 4) | lo);
    outSenderPrefix[i] = b;
    if (b != 0x00) anyNonZero = true;
    if (b != 0xFF) anyNonFF   = true;
  }
  if (!anyNonZero || !anyNonFF) return false;
  *outText = plain + 13;
  return true;
}

static void formatPublishDmJson(char* json, size_t cap,
                                const char* senderName, const uint8_t* senderKey32,
                                float snr, uint8_t hops, uint32_t ts, const char* text,
                                const char* dir = "rx", const char* recipientName = nullptr,
                                const uint8_t* recipientKey32 = nullptr) {
  char keyHex[13] = {};
  for (int i = 0; i < 6; ++i) std::snprintf(keyHex + i * 2, 3, "%02x", senderKey32[i]);
  const char* safeDir = dir ? dir : "rx";
  if (recipientName && recipientKey32) {
    char rcptKeyHex[13] = {};
    for (int i = 0; i < 6; ++i) std::snprintf(rcptKeyHex + i * 2, 3, "%02x", recipientKey32[i]);
    std::snprintf(json, cap,
        "{\"dir\":\"%s\",\"sender\":\"%s\",\"key\":\"%s\",\"recipient\":\"%s\",\"recipient_key\":\"%s\",\"snr\":%.1f,\"hops\":%u,\"ts\":%lu,\"text\":\"%s\"}",
        safeDir, senderName, keyHex, recipientName, rcptKeyHex, snr, (unsigned)hops, (unsigned long)ts, text);
  } else {
    std::snprintf(json, cap,
        "{\"dir\":\"%s\",\"sender\":\"%s\",\"key\":\"%s\",\"snr\":%.1f,\"hops\":%u,\"ts\":%lu,\"text\":\"%s\"}",
        safeDir, senderName, keyHex, snr, (unsigned)hops, (unsigned long)ts, text);
  }
}

void testMqttOptInAuthAndOutgoingIdentity() {
  // 1) Authenticated rx/dm payload requires "<12-hex-sender-prefix>:<non-empty-text>"
  uint8_t prefix[6]{};
  const char* text = nullptr;
  const char* valid_payload = "a1b2c3d4e5f6:Hello from authenticated peer";
  assert(parseAuthenticatedRxDm(valid_payload, std::strlen(valid_payload), prefix, &text));
  const uint8_t expected_prefix[6] = {0xA1, 0xB2, 0xC3, 0xD4, 0xE5, 0xF6};
  assert(std::memcmp(prefix, expected_prefix, 6) == 0);
  assert(std::strcmp(text, "Hello from authenticated peer") == 0);

  // Reject unauthenticated plain text without "<12-hex>:" header
  const char* unauth_plain = "Unauthenticated raw text without sender prefix";
  assert(!parseAuthenticatedRxDm(unauth_plain, std::strlen(unauth_plain), prefix, &text));

  // Reject all-zero or all-FF sender prefixes and empty message bodies
  const char* zero_prefix = "000000000000:spoof";
  assert(!parseAuthenticatedRxDm(zero_prefix, std::strlen(zero_prefix), prefix, &text));
  const char* ff_prefix = "ffffffffffff:spoof";
  assert(!parseAuthenticatedRxDm(ff_prefix, std::strlen(ff_prefix), prefix, &text));
  const char* empty_body = "a1b2c3d4e5f6:";
  assert(!parseAuthenticatedRxDm(empty_body, std::strlen(empty_body), prefix, &text));

  // 2) Outgoing DM publishes local node identity as sender/key with dir="tx" and recipient metadata
  const uint8_t local_pub[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
  const uint8_t rcpt_pub[32]  = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
  char tx_json[576]{};
  formatPublishDmJson(tx_json, sizeof(tx_json), "LocalNode", local_pub, 0.0f, 0xFF, 1794300000UL,
                      "outbound ping", "tx", "RemotePeer", rcpt_pub);
  const std::string tx_str(tx_json);
  assert(tx_str.find("\"dir\":\"tx\"") != std::string::npos);
  assert(tx_str.find("\"sender\":\"LocalNode\"") != std::string::npos);
  assert(tx_str.find("\"key\":\"112233445566\"") != std::string::npos);
  assert(tx_str.find("\"recipient\":\"RemotePeer\"") != std::string::npos);
  assert(tx_str.find("\"recipient_key\":\"aabbccddeeff\"") != std::string::npos);

  // Worst-case max-length sender (47), recipient (47), and text (299) must not truncate json[576]
  const std::string max_name(47, 'S');
  const std::string max_rcpt(47, 'R');
  const std::string max_text(299, 'M');
  char max_json[576]{};
  formatPublishDmJson(max_json, sizeof(max_json), max_name.c_str(), local_pub, -20.5f, 255,
                      1893456000UL, max_text.c_str(), "tx", max_rcpt.c_str(), rcpt_pub);
  const size_t max_len = std::strlen(max_json);
  assert(max_len > 480 && max_len < sizeof(max_json));
  assert(max_json[max_len - 1] == '}');

  // Incoming DM publishes remote sender with dir="rx"
  char rx_json[576]{};
  formatPublishDmJson(rx_json, sizeof(rx_json), "RemotePeer", rcpt_pub, 6.5f, 2, 1794300005UL,
                      "inbound reply");
  const std::string rx_str(rx_json);
  assert(rx_str.find("\"dir\":\"rx\"") != std::string::npos);
  assert(rx_str.find("\"sender\":\"RemotePeer\"") != std::string::npos);
  assert(rx_str.find("\"key\":\"aabbccddeeff\"") != std::string::npos);
  assert(rx_str.find("\"recipient\"") == std::string::npos);
}

// ============================================================================
// 6. Source-level contract checks across MyMesh, UITask, ClockFloorRTC, MqttBridge, and wardrive.lua
// ============================================================================

static std::string slurp(const char* path) {
  std::ifstream f(path);
  if (!f) {
    std::fprintf(stderr, "cannot read %s (run from repo root)\n", path);
    assert(false);
  }
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

void testSourceContracts() {
  const std::string clock_h      = slurp("src/helpers/ClockFloorRTC.h");
  const std::string mymesh_h     = slurp("src/MyMesh.h");
  const std::string mymesh_cpp   = slurp("src/MyMesh.cpp");
  const std::string mqtt_h       = slurp("src/helpers/esp32/MqttBridge.h");
  const std::string mqtt_cpp     = slurp("src/helpers/esp32/MqttBridge.cpp");
  const std::string mt_h         = slurp("src/helpers/esp32/MultiTransportCompanionInterface.h");
  const std::string mt_cpp       = slurp("src/helpers/esp32/MultiTransportCompanionInterface.cpp");
  const std::string uitask_cpp   = slurp("src/ui-touch/UITask.cpp");
  const std::string wardrive     = slurp("deploy/apps/wardrive/1.3/wardrive.lua");

  // ClockFloorRTC contracts:
  assert(clock_h.find("adoptMeshPeerTime") != std::string::npos);
  assert(clock_h.find("MAX_SINGLE_PEER_JUMP_SECS") != std::string::npos);
  assert(clock_h.find("PEER_QUORUM_TOLERANCE_SECS") != std::string::npos);

  // MyMesh.h & MyMesh.cpp contracts:
  assert(mymesh_h.find("path_len") != std::string::npos);
  assert(mymesh_h.find("isRepeaterPingCmd") != std::string::npos);
  assert(mymesh_h.find("uiSendRepeaterDmPing") != std::string::npos);
  assert(mymesh_cpp.find("discoverUpsert(&packet->payload[6], pklen, node_type, our_snr_q4, our_rssi, their_snr_q4, packet->path_len);") != std::string::npos);
  assert(mymesh_cpp.find("ROUTE_TYPE_TRANSPORT_FLOOD") != std::string::npos);
  assert(mymesh_cpp.find("if (cli_command[0] != '\\0')") != std::string::npos);
  assert(mymesh_cpp.find("rtc_clock.adoptMeshPeerTime(") != std::string::npos);
  assert(mymesh_cpp.find("formatRepeaterPongMessage") != std::string::npos);
  assert(mymesh_cpp.find("mqtt_bridge.publishDM(_prefs.node_name, self_id.pub_key, 0.0f, 0xFF, dbg_ptr->uniq_ts, text,") != std::string::npos);
  assert(mymesh_cpp.find("if (memcmp(sender_prefix6, self_id.pub_key, 6) == 0) return;") != std::string::npos);

  // MqttBridge security & privacy contracts:
  assert(mqtt_cpp.find("broker.emqx.io") == std::string::npos);
  assert(mqtt_cpp.find("kDefaultGatewayPrefix") == std::string::npos);
  assert(mqtt_cpp.find("_requestedEnabled = consent && p.getBool(\"en\", false);") != std::string::npos);
  assert(mqtt_cpp.find("_pubDm            = consent && p.getBool(\"dm\", false);") != std::string::npos);
  assert(mqtt_cpp.find("parseAuthenticatedRxDm") != std::string::npos);
  assert(mqtt_cpp.find("char json[576];") != std::string::npos);
  assert(mqtt_h.find("parseAuthenticatedRxDm") != std::string::npos);
  assert(mymesh_cpp.find("ensureRegionWithParents") != std::string::npos);
  assert(mymesh_cpp.find("kSeedRegions") == std::string::npos);
  assert(mymesh_cpp.find("isReceivingUsbFrame") == std::string::npos);
  assert(mymesh_cpp.find("MultiTransportCompanionInterface::isUsbFrameInProgress()") != std::string::npos);
  assert(mt_h.find("static bool isUsbFrameInProgress();") != std::string::npos);
  assert(mt_cpp.find("class UsbFrameTrackingStream : public Stream") != std::string::npos);

  // UITask.cpp contracts:
  assert(uitask_cpp.find("s_discover_tx_throttled") != std::string::npos);
  assert(uitask_cpp.find("Listening (TX pause)") != std::string::npos);
  assert(uitask_cpp.find("No trace reply (node out of direct/known path)") != std::string::npos);
  assert(uitask_cpp.find("static RecentlyHeardName heard[k_mention_source_max];") != std::string::npos);
  assert(uitask_cpp.find("static char line[640];") != std::string::npos);
  assert(uitask_cpp.find("uiSendRepeaterDmPing") != std::string::npos);

  // wardrive.lua contracts:
  assert(wardrive.find("last_harvested_ms") != std::string::npos);
}

}  // namespace

int main() {
  testDiscoverUpsertAndControlRecv();
  testDualScopeFloodRetry();
  testSplitLineSerialCliAssembly();
  testUsbCompanionSplitFrameGuard();
  testClockFloorAndMeshQuorum();
  testRepeaterDmPingAndPongFormat();
  testMqttOptInAuthAndOutgoingIdentity();
  testSourceContracts();
  std::printf("host_m9_audit_test: ALL PASSED\n");
  return 0;
}
