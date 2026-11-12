// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Clean-room Meshtastic over-the-air (OTA) wire format, AES-128-CTR stream
// cipher, and minimal Protobuf3 codec for WadaMesh cross-play (`crossmesh`).
// Pure header-only C++17 with zero external dependencies so it compiles and
// runs identically in macOS host tests (`test/test_meshtastic_wire.cpp`) and
// on ESP32 / nRF52 targets.
namespace MeshtasticWire {

static constexpr uint32_t kBroadcastAddr = 0xFFFFFFFFu;
static constexpr size_t   kHeaderSize    = 16;
static constexpr size_t   kMaxPayload    = 237;
static constexpr uint8_t  kSyncWord      = 0x2B;   // RadioLib setSyncWord(0x2B, 0x44) -> 0x24B4

// Default Meshtastic 1-byte PSK shorthand `0x01` ("AQ==") expanded to its
// standard 16-byte AES-128 key.
static constexpr uint8_t kDefaultLongFastKey[16] = {
  0xd4, 0xf1, 0xbb, 0x3a, 0x20, 0x29, 0x07, 0x59,
  0xf0, 0xbc, 0xff, 0xab, 0xcf, 0x4e, 0x69, 0x01
};

enum PortNum : uint16_t {
  PORT_UNKNOWN    = 0,
  PORT_TEXT       = 1,    // TEXT_MESSAGE_APP (UTF-8 payload)
  PORT_POSITION   = 3,    // POSITION_APP (meshtastic.Position)
  PORT_NODEINFO   = 4,    // NODEINFO_APP (meshtastic.User)
  PORT_ROUTING    = 5,    // ROUTING_APP (meshtastic.Routing ACK / error)
  PORT_TELEMETRY  = 67,   // TELEMETRY_APP
  PORT_TRACEROUTE = 70,   // TRACEROUTE_APP (meshtastic.RouteDiscovery)
};

struct Header {
  uint32_t to;
  uint32_t from;
  uint32_t id;
  uint8_t  hop_limit;
  uint8_t  hop_start;
  bool     want_ack;
  bool     via_mqtt;
  uint8_t  channel;
  uint8_t  next_hop;
  uint8_t  relay_node;
};

struct DecodedPacket {
  Header   hdr;
  uint16_t port;
  bool     want_response;
  uint32_t dest;
  uint32_t source;
  uint32_t request_id;
  uint32_t reply_id;
  // PORT_TEXT
  char     text[200];
  // PORT_POSITION
  int32_t  lat_i;       // deg * 1e7
  int32_t  lon_i;       // deg * 1e7
  int32_t  alt_m;
  uint32_t pos_time;
  bool     has_pos;
  // PORT_NODEINFO
  char     user_id[16];
  char     long_name[40];
  char     short_name[6];
  uint16_t hw_model;
  uint8_t  role;
  // PORT_ROUTING
  uint8_t  routing_err;
  // PORT_TRACEROUTE
  uint8_t  route_cnt;
  uint8_t  snr_towards_cnt;
  uint8_t  route_back_cnt;
  uint8_t  snr_back_cnt;
  uint32_t route[8];
  uint32_t route_back[8];
  int8_t   snr_towards[8];   // SNR * 4 (dB * 4)
  int8_t   snr_back[8];
};

static inline uint32_t rdLE32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline void wrLE32(uint8_t* p, uint32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

// Derive a deterministic unicast 32-bit Meshtastic NodeNum from a 32-byte
// MeshCore Ed25519 public key (last 4 bytes big-endian so `!xxxxxxxx` matches
// the suffix of the hex key). Remaps reserved 0 and 0xFFFFFFFF to valid unicast.
static inline uint32_t nodeIdFromPubKey(const uint8_t pubkey[32]) {
  if (!pubkey) return 0x4D430001u;
  uint32_t id = ((uint32_t)pubkey[28] << 24) | ((uint32_t)pubkey[29] << 16) |
                ((uint32_t)pubkey[30] << 8)  | (uint32_t)pubkey[31];
  if (id == 0u || id == kBroadcastAddr || id < 4u) {
    uint32_t h = 2166136261u;
    for (int i = 0; i < 32; ++i) h = (h ^ pubkey[i]) * 16777619u;
    id = (h == 0u || h == kBroadcastAddr || h < 4u) ? 0x4D430001u : h;
  }
  return id;
}

static inline uint8_t xorHash(const uint8_t* p, size_t n) {
  uint8_t h = 0;
  for (size_t i = 0; i < n; ++i) h ^= p[i];
  return h;
}

static inline uint8_t computeChannelHash(const char* name, const uint8_t* key, size_t key_len) {
  if (!name) return 0;
  uint8_t h = xorHash((const uint8_t*)name, strlen(name));
  if (key && key_len > 0) h ^= xorHash(key, key_len);
  return h;
}

static inline void packHeader(uint8_t out[16], const Header& h) {
  wrLE32(out + 0, h.to);
  wrLE32(out + 4, h.from);
  wrLE32(out + 8, h.id);
  uint8_t flags = (uint8_t)(h.hop_limit & 0x07);
  if (h.want_ack) flags |= 0x08;
  if (h.via_mqtt) flags |= 0x10;
  flags |= (uint8_t)((h.hop_start & 0x07) << 5);
  out[12] = flags;
  out[13] = h.channel;
  out[14] = h.next_hop;
  out[15] = h.relay_node;
}

static inline bool unpackHeader(const uint8_t* in, size_t len, Header& h) {
  if (!in || len < kHeaderSize) return false;
  h.to         = rdLE32(in + 0);
  h.from       = rdLE32(in + 4);
  h.id         = rdLE32(in + 8);
  uint8_t fl   = in[12];
  h.hop_limit  = fl & 0x07;
  h.want_ack   = (fl & 0x08) != 0;
  h.via_mqtt   = (fl & 0x10) != 0;
  h.hop_start  = (fl >> 5) & 0x07;
  h.channel    = in[13];
  h.next_hop   = in[14];
  h.relay_node = in[15];
  return true;
}

// ---- Portable AES-128 block cipher + CTR stream mode ------------------------
inline uint8_t aesSbox(uint8_t x) {
  static const uint8_t kS[256] = {
    0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,
    0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
    0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,
    0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
    0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,
    0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
    0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,
    0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
    0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,
    0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
    0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,
    0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
    0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,
    0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
    0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,
    0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16
  };
  return kS[x];
}

static inline uint8_t gfMul2(uint8_t x) {
  return (uint8_t)((x << 1) ^ ((x & 0x80) ? 0x1Bu : 0x00u));
}

inline void aes128EncryptBlock(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
  uint8_t rk[176];
  memcpy(rk, key, 16);
  uint8_t rcon = 0x01;
  for (int i = 16; i < 176; i += 4) {
    uint8_t t[4] = { rk[i - 4], rk[i - 3], rk[i - 2], rk[i - 1] };
    if ((i & 15) == 0) {
      uint8_t tmp = t[0];
      t[0] = aesSbox(t[1]) ^ rcon;
      t[1] = aesSbox(t[2]);
      t[2] = aesSbox(t[3]);
      t[3] = aesSbox(tmp);
      rcon = gfMul2(rcon);
    }
    for (int k = 0; k < 4; ++k) rk[i + k] = rk[i - 16 + k] ^ t[k];
  }

  uint8_t s[16];
  for (int i = 0; i < 16; ++i) s[i] = in[i] ^ rk[i];

  for (int r = 1; r <= 10; ++r) {
    for (int i = 0; i < 16; ++i) s[i] = aesSbox(s[i]);
    uint8_t t;
    t = s[1]; s[1] = s[5]; s[5] = s[9];  s[9]  = s[13]; s[13] = t;
    t = s[2]; s[2] = s[10]; s[10] = t;   t = s[6]; s[6] = s[14]; s[14] = t;
    t = s[15]; s[15] = s[11]; s[11] = s[7]; s[7] = s[3]; s[3] = t;
    if (r < 10) {
      for (int c = 0; c < 16; c += 4) {
        uint8_t a0 = s[c], a1 = s[c + 1], a2 = s[c + 2], a3 = s[c + 3];
        uint8_t x  = a0 ^ a1 ^ a2 ^ a3;
        s[c + 0] ^= x ^ gfMul2(a0 ^ a1);
        s[c + 1] ^= x ^ gfMul2(a1 ^ a2);
        s[c + 2] ^= x ^ gfMul2(a2 ^ a3);
        s[c + 3] ^= x ^ gfMul2(a3 ^ a0);
      }
    }
    const uint8_t* rkp = rk + (r * 16);
    for (int i = 0; i < 16; ++i) s[i] ^= rkp[i];
  }
  memcpy(out, s, 16);
}

static inline void initNonce(uint8_t nonce[16], uint32_t packet_id, uint32_t from_node) {
  memset(nonce, 0, 16);
  wrLE32(nonce + 0, packet_id);
  wrLE32(nonce + 8, from_node);
}

static inline void aes128CtrCrypt(const uint8_t key[16], const uint8_t nonce[16],
                                  const uint8_t* in, uint8_t* out, size_t len) {
  uint8_t ctr[16], ks[16];
  memcpy(ctr, nonce, 16);
  size_t off = 0;
  while (off < len) {
    aes128EncryptBlock(key, ctr, ks);
    size_t chunk = (len - off < 16) ? (len - off) : 16;
    for (size_t i = 0; i < chunk; ++i) out[off + i] = in[off + i] ^ ks[i];
    off += chunk;
    // Standard big-endian counter increment in the last 4 bytes (Meshtastic / mbedtls convention).
    for (int k = 15; k >= 12; --k) {
      if (++ctr[k] != 0) break;
    }
  }
}

// ---- Minimal Protobuf3 wire helpers ----------------------------------------
static inline bool pbReadVarint(const uint8_t* buf, size_t len, size_t& pos, uint64_t& val) {
  val = 0;
  int shift = 0;
  while (pos < len && shift < 64) {
    uint8_t b = buf[pos++];
    val |= (uint64_t)(b & 0x7Fu) << shift;
    if ((b & 0x80u) == 0) return true;
    shift += 7;
  }
  return false;
}

static inline bool pbWriteVarint(uint8_t* buf, size_t cap, size_t& pos, uint64_t val) {
  do {
    if (pos >= cap) return false;
    uint8_t b = (uint8_t)(val & 0x7Fu);
    val >>= 7;
    if (val) b |= 0x80u;
    buf[pos++] = b;
  } while (val);
  return true;
}

static inline bool pbWriteTag(uint8_t* buf, size_t cap, size_t& pos, uint32_t field, uint8_t wtype) {
  return pbWriteVarint(buf, cap, pos, ((uint64_t)field << 3) | (wtype & 0x07u));
}

static inline bool pbWriteBytes(uint8_t* buf, size_t cap, size_t& pos, uint32_t field,
                                const void* data, size_t n) {
  if (!pbWriteTag(buf, cap, pos, field, 2)) return false;
  if (!pbWriteVarint(buf, cap, pos, n)) return false;
  if (pos + n > cap) return false;
  if (n > 0) memcpy(buf + pos, data, n);
  pos += n;
  return true;
}

static inline bool pbWriteFixed32(uint8_t* buf, size_t cap, size_t& pos, uint32_t field, uint32_t v) {
  if (!pbWriteTag(buf, cap, pos, field, 5)) return false;
  if (pos + 4 > cap) return false;
  wrLE32(buf + pos, v);
  pos += 4;
  return true;
}

static inline bool pbSkipField(const uint8_t* buf, size_t len, size_t& pos, uint8_t wtype) {
  uint64_t v = 0;
  switch (wtype) {
    case 0: return pbReadVarint(buf, len, pos, v);
    case 1: if (pos + 8 > len) return false; pos += 8; return true;
    case 2: if (!pbReadVarint(buf, len, pos, v) || v > len - pos) return false; pos += (size_t)v; return true;
    case 5: if (pos + 4 > len) return false; pos += 4; return true;
    default: return false;
  }
}

static inline bool decodePositionSub(const uint8_t* b, size_t len, DecodedPacket& out) {
  size_t pos = 0;
  while (pos < len) {
    uint64_t tag = 0, v = 0;
    if (!pbReadVarint(b, len, pos, tag) || tag == 0) return false;
    uint32_t fno = (uint32_t)(tag >> 3);
    uint8_t  wt  = (uint8_t)(tag & 0x07);
    if (fno == 1 && wt == 5) {
      if (pos + 4 > len) return false;
      out.lat_i = (int32_t)rdLE32(b + pos); pos += 4; out.has_pos = true;
    } else if (fno == 2 && wt == 5) {
      if (pos + 4 > len) return false;
      out.lon_i = (int32_t)rdLE32(b + pos); pos += 4; out.has_pos = true;
    } else if (fno == 3 && wt == 0) {
      if (!pbReadVarint(b, len, pos, v)) return false;
      out.alt_m = (int32_t)(uint32_t)v;
    } else if (fno == 4 && (wt == 5 || wt == 0)) {
      if (wt == 5) { if (pos + 4 > len) return false; out.pos_time = rdLE32(b + pos); pos += 4; }
      else { if (!pbReadVarint(b, len, pos, v)) return false; out.pos_time = (uint32_t)v; }
    } else {
      if (!pbSkipField(b, len, pos, wt)) return false;
    }
  }
  return true;
}

static inline bool decodeUserSub(const uint8_t* b, size_t len, DecodedPacket& out) {
  size_t pos = 0;
  while (pos < len) {
    uint64_t tag = 0, v = 0;
    if (!pbReadVarint(b, len, pos, tag) || tag == 0) return false;
    uint32_t fno = (uint32_t)(tag >> 3);
    uint8_t  wt  = (uint8_t)(tag & 0x07);
    if ((fno == 1 || fno == 2 || fno == 3) && wt == 2) {
      if (!pbReadVarint(b, len, pos, v) || v > len - pos) return false;
      char* dst = (fno == 1) ? out.user_id : (fno == 2 ? out.long_name : out.short_name);
      size_t cap = (fno == 1) ? sizeof(out.user_id) : (fno == 2 ? sizeof(out.long_name) : sizeof(out.short_name));
      size_t cpy = (v < cap - 1) ? (size_t)v : (cap - 1);
      memcpy(dst, b + pos, cpy);
      dst[cpy] = '\0';
      pos += (size_t)v;
    } else if (fno == 5 && wt == 0) {
      if (!pbReadVarint(b, len, pos, v)) return false;
      out.hw_model = (uint16_t)v;
    } else if (fno == 7 && wt == 0) {
      if (!pbReadVarint(b, len, pos, v)) return false;
      out.role = (uint8_t)v;
    } else {
      if (!pbSkipField(b, len, pos, wt)) return false;
    }
  }
  return true;
}

static inline bool decodeRoutingSub(const uint8_t* b, size_t len, DecodedPacket& out) {
  size_t pos = 0;
  while (pos < len) {
    uint64_t tag = 0, v = 0;
    if (!pbReadVarint(b, len, pos, tag) || tag == 0) return false;
    uint32_t fno = (uint32_t)(tag >> 3);
    uint8_t  wt  = (uint8_t)(tag & 0x07);
    if (fno == 3 && wt == 0) {
      if (!pbReadVarint(b, len, pos, v)) return false;
      out.routing_err = (uint8_t)v;
    } else {
      if (!pbSkipField(b, len, pos, wt)) return false;
    }
  }
  return true;
}

static inline bool decodeRouteDiscoverySub(const uint8_t* b, size_t len, DecodedPacket& out) {
  size_t pos = 0;
  while (pos < len) {
    uint64_t tag = 0, v = 0;
    if (!pbReadVarint(b, len, pos, tag) || tag == 0) return false;
    uint32_t fno = (uint32_t)(tag >> 3);
    uint8_t  wt  = (uint8_t)(tag & 0x07);
    if ((fno == 1 || fno == 3) && (wt == 2 || wt == 5 || wt == 0)) {
      uint32_t* arr = (fno == 1) ? out.route : out.route_back;
      uint8_t&  cnt = (fno == 1) ? out.route_cnt : out.route_back_cnt;
      if (wt == 2) {
        if (!pbReadVarint(b, len, pos, v) || v > len - pos) return false;
        size_t end = pos + (size_t)v;
        // Meshtastic RouteDiscovery.route is repeated fixed32 (or varint in older builds).
        if ((v % 4) == 0) {
          while (pos + 4 <= end) {
            uint32_t node = rdLE32(b + pos); pos += 4;
            if (cnt < 8) arr[cnt++] = node;
          }
        } else {
          while (pos < end) {
            uint64_t nv = 0;
            if (!pbReadVarint(b, end, pos, nv)) return false;
            if (cnt < 8) arr[cnt++] = (uint32_t)nv;
          }
        }
      } else if (wt == 5) {
        if (pos + 4 > len) return false;
        if (cnt < 8) arr[cnt++] = rdLE32(b + pos);
        pos += 4;
      } else {
        if (!pbReadVarint(b, len, pos, v)) return false;
        if (cnt < 8) arr[cnt++] = (uint32_t)v;
      }
    } else if ((fno == 2 || fno == 4) && (wt == 2 || wt == 0)) {
      int8_t*  arr = (fno == 2) ? out.snr_towards : out.snr_back;
      uint8_t& cnt = (fno == 2) ? out.snr_towards_cnt : out.snr_back_cnt;
      if (wt == 2) {
        if (!pbReadVarint(b, len, pos, v) || v > len - pos) return false;
        size_t end = pos + (size_t)v;
        while (pos < end) {
          uint64_t sv = 0;
          if (!pbReadVarint(b, end, pos, sv)) return false;
          if (cnt < 8) arr[cnt++] = (int8_t)(int32_t)(uint32_t)sv;
        }
      } else {
        if (!pbReadVarint(b, len, pos, v)) return false;
        if (cnt < 8) arr[cnt++] = (int8_t)(int32_t)(uint32_t)v;
      }
    } else {
      if (!pbSkipField(b, len, pos, wt)) return false;
    }
  }
  return true;
}

inline bool decodePacket(const uint8_t* raw, size_t raw_len, const uint8_t key[16],
                         uint8_t expected_hash, DecodedPacket& out) {
  memset(&out, 0, sizeof out);
  if (!raw || raw_len <= kHeaderSize || raw_len > kHeaderSize + kMaxPayload) return false;
  if (!unpackHeader(raw, raw_len, out.hdr)) return false;
  if (out.hdr.channel != expected_hash) return false;

  const size_t ct_len = raw_len - kHeaderSize;
  uint8_t pt[kMaxPayload];
  uint8_t nonce[16];
  initNonce(nonce, out.hdr.id, out.hdr.from);
  aes128CtrCrypt(key, nonce, raw + kHeaderSize, pt, ct_len);

  const uint8_t* payload_ptr = nullptr;
  size_t payload_len = 0;
  size_t pos = 0;
  while (pos < ct_len) {
    uint64_t tag = 0, v = 0;
    if (!pbReadVarint(pt, ct_len, pos, tag) || tag == 0) return false;
    uint32_t fno = (uint32_t)(tag >> 3);
    uint8_t  wt  = (uint8_t)(tag & 0x07);
    if (fno == 0 || fno > 16) return false;   // reject garbage decrypt
    if (fno == 1 && wt == 0) {
      if (!pbReadVarint(pt, ct_len, pos, v) || v > 512) return false;
      out.port = (uint16_t)v;
    } else if (fno == 2 && wt == 2) {
      if (!pbReadVarint(pt, ct_len, pos, v) || v > ct_len - pos) return false;
      payload_ptr = pt + pos;
      payload_len = (size_t)v;
      pos += (size_t)v;
    } else if (fno == 3 && wt == 0) {
      if (!pbReadVarint(pt, ct_len, pos, v)) return false;
      out.want_response = (v != 0);
    } else if (fno == 4 && wt == 5) {
      if (pos + 4 > ct_len) return false;
      out.dest = rdLE32(pt + pos); pos += 4;
    } else if (fno == 5 && wt == 5) {
      if (pos + 4 > ct_len) return false;
      out.source = rdLE32(pt + pos); pos += 4;
    } else if (fno == 6 && wt == 5) {
      if (pos + 4 > ct_len) return false;
      out.request_id = rdLE32(pt + pos); pos += 4;
    } else if (fno == 7 && wt == 5) {
      if (pos + 4 > ct_len) return false;
      out.reply_id = rdLE32(pt + pos); pos += 4;
    } else {
      if (!pbSkipField(pt, ct_len, pos, wt)) return false;
    }
  }

  if (out.port == PORT_UNKNOWN) return false;

  if (out.port == PORT_TEXT && payload_ptr) {
    size_t cpy = (payload_len < sizeof(out.text) - 1) ? payload_len : (sizeof(out.text) - 1);
    memcpy(out.text, payload_ptr, cpy);
    out.text[cpy] = '\0';
  } else if (out.port == PORT_POSITION && payload_ptr) {
    if (!decodePositionSub(payload_ptr, payload_len, out)) return false;
  } else if (out.port == PORT_NODEINFO && payload_ptr) {
    if (!decodeUserSub(payload_ptr, payload_len, out)) return false;
  } else if (out.port == PORT_ROUTING && payload_ptr) {
    if (!decodeRoutingSub(payload_ptr, payload_len, out)) return false;
  } else if (out.port == PORT_TRACEROUTE && payload_ptr) {
    if (!decodeRouteDiscoverySub(payload_ptr, payload_len, out)) return false;
  }
  return true;
}

static inline size_t sealDataFrame(uint8_t* out, size_t cap, const Header& h,
                                   const uint8_t key[16], uint16_t port,
                                   const uint8_t* sub, size_t sub_len,
                                   bool want_resp = false, uint32_t req_id = 0) {
  uint8_t pt[kMaxPayload];
  size_t pos = 0;
  if (!pbWriteTag(pt, sizeof pt, pos, 1, 0) || !pbWriteVarint(pt, sizeof pt, pos, port)) return 0;
  if (!pbWriteBytes(pt, sizeof pt, pos, 2, sub, sub_len)) return 0;
  if (want_resp) {
    if (!pbWriteTag(pt, sizeof pt, pos, 3, 0) || !pbWriteVarint(pt, sizeof pt, pos, 1)) return 0;
  }
  if (req_id != 0) {
    if (!pbWriteFixed32(pt, sizeof pt, pos, 6, req_id)) return 0;
  }
  if (kHeaderSize + pos > cap) return 0;
  packHeader(out, h);
  uint8_t nonce[16];
  initNonce(nonce, h.id, h.from);
  aes128CtrCrypt(key, nonce, pt, out + kHeaderSize, pos);
  return kHeaderSize + pos;
}

inline size_t encodeTextPacket(uint8_t* out, size_t cap, uint32_t from, uint32_t to, uint32_t id,
                               uint8_t ch_hash, const uint8_t key[16], const char* text, bool want_ack) {
  if (!text) return 0;
  size_t tlen = strlen(text);
  if (tlen == 0 || tlen > 200) return 0;
  Header h = { to, from, id, 3, 3, want_ack, false, ch_hash, 0, (uint8_t)(from & 0xFF) };
  return sealDataFrame(out, cap, h, key, PORT_TEXT, (const uint8_t*)text, tlen, false, 0);
}

inline size_t encodePositionPacket(uint8_t* out, size_t cap, uint32_t from, uint32_t id,
                                   uint8_t ch_hash, const uint8_t key[16],
                                   int32_t lat_e7, int32_t lon_e7, int32_t alt_m, uint32_t time_s) {
  uint8_t sub[48];
  size_t pos = 0;
  if (!pbWriteFixed32(sub, sizeof sub, pos, 1, (uint32_t)lat_e7)) return 0;
  if (!pbWriteFixed32(sub, sizeof sub, pos, 2, (uint32_t)lon_e7)) return 0;
  if (alt_m != 0) {
    if (!pbWriteTag(sub, sizeof sub, pos, 3, 0) ||
        !pbWriteVarint(sub, sizeof sub, pos, (uint64_t)(int64_t)alt_m)) return 0;
  }
  if (time_s != 0) {
    if (!pbWriteFixed32(sub, sizeof sub, pos, 4, time_s)) return 0;
  }
  Header h = { kBroadcastAddr, from, id, 3, 3, false, false, ch_hash, 0, (uint8_t)(from & 0xFF) };
  return sealDataFrame(out, cap, h, key, PORT_POSITION, sub, pos, false, 0);
}

inline size_t encodeNodeInfoPacket(uint8_t* out, size_t cap, uint32_t from, uint32_t id,
                                   uint8_t ch_hash, const uint8_t key[16],
                                   const char* long_name, const char* short_name) {
  uint8_t sub[96];
  size_t pos = 0;
  char uid[12];
  snprintf(uid, sizeof uid, "!%08x", (unsigned)from);
  if (!pbWriteBytes(sub, sizeof sub, pos, 1, uid, strlen(uid))) return 0;
  if (long_name && *long_name) {
    size_t l = strlen(long_name);
    if (l > 36) l = 36;
    if (!pbWriteBytes(sub, sizeof sub, pos, 2, long_name, l)) return 0;
  }
  if (short_name && *short_name) {
    size_t l = strlen(short_name);
    if (l > 4) l = 4;
    if (!pbWriteBytes(sub, sizeof sub, pos, 3, short_name, l)) return 0;
  }
  // hw_model = 255 (PRIVATE_HW) so Meshtastic clients display a clean custom icon.
  if (!pbWriteTag(sub, sizeof sub, pos, 5, 0) || !pbWriteVarint(sub, sizeof sub, pos, 255)) return 0;
  Header h = { kBroadcastAddr, from, id, 3, 3, false, false, ch_hash, 0, (uint8_t)(from & 0xFF) };
  return sealDataFrame(out, cap, h, key, PORT_NODEINFO, sub, pos, false, 0);
}

inline size_t encodeRoutingAckPacket(uint8_t* out, size_t cap, uint32_t from, uint32_t to,
                                     uint32_t id, uint32_t req_id, uint8_t ch_hash,
                                     const uint8_t key[16], uint8_t err_reason) {
  uint8_t sub[8];
  size_t pos = 0;
  if (!pbWriteTag(sub, sizeof sub, pos, 3, 0) || !pbWriteVarint(sub, sizeof sub, pos, err_reason)) return 0;
  Header h = { to, from, id, 3, 3, false, false, ch_hash, 0, (uint8_t)(from & 0xFF) };
  return sealDataFrame(out, cap, h, key, PORT_ROUTING, sub, pos, false, req_id);
}

inline size_t encodeTraceroutePacket(uint8_t* out, size_t cap, uint32_t from, uint32_t to,
                                     uint32_t id, uint8_t ch_hash, const uint8_t key[16]) {
  // Empty RouteDiscovery message with Data.want_response = true; routers append
  // their fixed32 NodeNum and SNR*4 along the path and the target replies.
  uint8_t sub[4] = {0};
  Header h = { to, from, id, 3, 3, true, false, ch_hash, 0, (uint8_t)(from & 0xFF) };
  return sealDataFrame(out, cap, h, key, PORT_TRACEROUTE, sub, 0, true, 0);
}

inline size_t encodeTracerouteReplyForTest(uint8_t* out, size_t cap, uint32_t from, uint32_t to,
                                           uint32_t id, uint32_t req_id, uint8_t ch_hash,
                                           const uint8_t key[16],
                                           const uint32_t* hops, size_t nhops,
                                           const int8_t* snrs, size_t nsnrs) {
  uint8_t sub[128];
  size_t pos = 0;
  if (nhops > 0) {
    uint8_t packed_hops[32];
    for (size_t i = 0; i < nhops; ++i) wrLE32(packed_hops + i * 4, hops[i]);
    if (!pbWriteBytes(sub, sizeof sub, pos, 1, packed_hops, nhops * 4)) return 0;
  }
  if (nsnrs > 0) {
    uint8_t packed_snrs[80];
    size_t spos = 0;
    for (size_t i = 0; i < nsnrs; ++i) {
      if (!pbWriteVarint(packed_snrs, sizeof packed_snrs, spos, (uint64_t)(int64_t)snrs[i])) return 0;
    }
    if (!pbWriteBytes(sub, sizeof sub, pos, 2, packed_snrs, spos)) return 0;
  }
  Header h = { to, from, id, 3, 3, false, false, ch_hash, 0, (uint8_t)(from & 0xFF) };
  return sealDataFrame(out, cap, h, key, PORT_TRACEROUTE, sub, pos, false, req_id);
}

}  // namespace MeshtasticWire
