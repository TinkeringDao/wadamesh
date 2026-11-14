// SPDX-License-Identifier: GPL-3.0-or-later
//
// Host test for MeshtasticWire.h:
//   - 16-byte Meshtastic OTA header pack/unpack
//   - Default LongFast PSK ("AQ==" -> 16-byte key) & channel hash (0x08)
//   - Deterministic unicast NodeNum derivation from MeshCore Ed25519 pubkey
//   - Portable AES-128-CTR stream encryption/decryption (NIST SP 800-38A vector + nonce)
//   - Protobuf Data wrapper + PortNum 1 (TEXT), 3 (POSITION), 4 (NODEINFO),
//     5 (ROUTING ACK), and 70 (TRACEROUTE RouteDiscovery)
//   - Bounds & malformed-input rejection
//
//   g++ -std=c++17 -Wall -Wextra -I src test/test_meshtastic_wire.cpp -o /tmp/mtw && /tmp/mtw

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "helpers/MeshtasticWire.h"

using namespace MeshtasticWire;

int main() {
  // 1. Default LongFast channel hash ("LongFast" + default 16-byte key -> 0x08).
  assert(computeChannelHash("LongFast", kDefaultLongFastKey, 16) == 0x08);

  // 2. NodeID derivation from MeshCore 32-byte public key, including 0 / 0xFFFFFFFF guards.
  uint8_t pk[32] = {0};
  pk[28] = 0x43; pk[29] = 0x6B; pk[30] = 0xCB; pk[31] = 0x27;
  assert(nodeIdFromPubKey(pk) == 0x436BCB27u);

  memset(pk, 0, sizeof pk);
  uint32_t id_zero = nodeIdFromPubKey(pk);
  assert(id_zero != 0u && id_zero != 0xFFFFFFFFu);

  memset(pk, 0xFF, sizeof pk);
  uint32_t id_bcast = nodeIdFromPubKey(pk);
  assert(id_bcast != 0u && id_bcast != 0xFFFFFFFFu);

  // 3. NIST SP 800-38A F.1.1 AES-128 ECB single-block sanity check.
  const uint8_t nist_key[16] = {
    0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
    0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
  };
  const uint8_t nist_pt[16] = {
    0x6b, 0xc1, 0xbe, 0xe2, 0x2e, 0x40, 0x9f, 0x96,
    0xe9, 0x3d, 0x7e, 0x11, 0x73, 0x93, 0x17, 0x2a
  };
  const uint8_t nist_ct[16] = {
    0x3a, 0xd7, 0x7b, 0xb4, 0x0d, 0x7a, 0x36, 0x60,
    0xa8, 0x9e, 0xca, 0xf3, 0x24, 0x66, 0xef, 0x97
  };
  uint8_t blk[16] = {0};
  aes128EncryptBlock(nist_key, nist_pt, blk);
  assert(memcmp(blk, nist_ct, 16) == 0);

  // 4. Nonce layout: packet_id (LE 64-bit) at 0..7, from_node (LE 32-bit) at 8..11, zero at 12..15.
  uint8_t nonce[16];
  initNonce(nonce, 0x11223344u, 0xAABBCCDDu);
  assert(nonce[0] == 0x44 && nonce[1] == 0x33 && nonce[2] == 0x22 && nonce[3] == 0x11);
  assert(nonce[4] == 0 && nonce[5] == 0 && nonce[6] == 0 && nonce[7] == 0);
  assert(nonce[8] == 0xDD && nonce[9] == 0xCC && nonce[10] == 0xBB && nonce[11] == 0xAA);
  assert(nonce[12] == 0 && nonce[13] == 0 && nonce[14] == 0 && nonce[15] == 0);

  // 5. TEXT_MESSAGE_APP (portnum 1) round-trip.
  uint8_t raw[256];
  size_t n = encodeTextPacket(raw, sizeof raw, 0x436BCB27u, kBroadcastAddr, 0xCAFE1234u,
                              0x08, kDefaultLongFastKey, "Servus from MeshCore!", true);
  assert(n > 16);
  DecodedPacket dec;
  assert(decodePacket(raw, n, kDefaultLongFastKey, 0x08, dec));
  assert(dec.hdr.from == 0x436BCB27u && dec.hdr.to == kBroadcastAddr && dec.hdr.id == 0xCAFE1234u);
  assert(dec.hdr.want_ack && dec.hdr.hop_limit == 3 && dec.hdr.hop_start == 3);
  assert(dec.port == PORT_TEXT);
  assert(strcmp(dec.text, "Servus from MeshCore!") == 0);

  // 6. POSITION_APP (portnum 3) round-trip (including negative sfixed32 coordinates).
  n = encodePositionPacket(raw, sizeof raw, 0x436BCB27u, 0x00010002u,
                           0x08, kDefaultLongFastKey, 483700027, -338688197, 518, 1762950000u);
  assert(n > 16);
  assert(decodePacket(raw, n, kDefaultLongFastKey, 0x08, dec));
  assert(dec.port == PORT_POSITION && dec.has_pos);
  assert(dec.lat_i == 483700027 && dec.lon_i == -338688197 && dec.alt_m == 518);

  // 7. NODEINFO_APP (portnum 4) round-trip.
  n = encodeNodeInfoPacket(raw, sizeof raw, 0x436BCB27u, 0x00010003u,
                           0x08, kDefaultLongFastKey, "Satori [MC]", "SATM");
  assert(n > 16);
  assert(decodePacket(raw, n, kDefaultLongFastKey, 0x08, dec));
  assert(dec.port == PORT_NODEINFO);
  assert(strcmp(dec.user_id, "!436bcb27") == 0);
  assert(strcmp(dec.long_name, "Satori [MC]") == 0);
  assert(strcmp(dec.short_name, "SATM") == 0);

  // 8. ROUTING_APP (portnum 5) ACK round-trip.
  n = encodeRoutingAckPacket(raw, sizeof raw, 0xdeadbeefu, 0x436BCB27u, 0x00010004u,
                             0xCAFE1234u, 0x08, kDefaultLongFastKey, 0);
  assert(n > 16);
  assert(decodePacket(raw, n, kDefaultLongFastKey, 0x08, dec));
  assert(dec.port == PORT_ROUTING);
  assert(dec.request_id == 0xCAFE1234u && dec.routing_err == 0);

  // 9. TRACEROUTE_APP (portnum 70) request + response with negative SNR varints.
  n = encodeTraceroutePacket(raw, sizeof raw, 0x436BCB27u, 0xdeadbeefu, 0x00010005u,
                             0x08, kDefaultLongFastKey);
  assert(n > 16);
  assert(decodePacket(raw, n, kDefaultLongFastKey, 0x08, dec));
  assert(dec.port == PORT_TRACEROUTE && dec.want_response);

  const uint32_t hops[2] = { 0x11111111u, 0x22222222u };
  const int8_t snrs[2]   = { 24, -18 };   // +6.0 dB, -4.5 dB
  n = encodeTracerouteReplyForTest(raw, sizeof raw, 0xdeadbeefu, 0x436BCB27u, 0x00010006u,
                                   0x00010005u, 0x08, kDefaultLongFastKey, hops, 2, snrs, 2);
  assert(n > 16);
  assert(decodePacket(raw, n, kDefaultLongFastKey, 0x08, dec));
  assert(dec.port == PORT_TRACEROUTE && dec.request_id == 0x00010005u);
  assert(dec.route_cnt == 2 && dec.route[0] == 0x11111111u && dec.route[1] == 0x22222222u);
  assert(dec.snr_towards[0] == 24 && dec.snr_towards[1] == -18);

  // 10. Bounds and malformed-frame rejection (Review Focus #1).
  assert(!decodePacket(raw, 15, kDefaultLongFastKey, 0x08, dec));
  assert(!decodePacket(raw, n, kDefaultLongFastKey, 0x99, dec));   // wrong channel hash
  uint8_t bad_key[16] = {0};
  assert(!decodePacket(raw, n, bad_key, 0x08, dec));                // garbage decrypt -> invalid protobuf

  printf("test_meshtastic_wire: all assertions passed\n");
  return 0;
}
