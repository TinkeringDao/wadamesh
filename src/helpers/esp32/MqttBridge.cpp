#if defined(ESP32) && defined(MULTI_TRANSPORT_COMPANION)
#include "MqttBridge.h"

// The single global bridge instance (extern in the header). Defined for every board: the header
// makes MqttBridge a no-op stub on the Tanmatsu, and the real bridge below on all other boards.
MqttBridge mqtt_bridge;

#if !defined(HAS_TANMATSU) && !defined(HAS_TDISPLAY_P4) && !defined(HAS_CROWPANEL_P4_101)   // ---- real PubSubClient/WiFiClient implementation; NOT built on the P4 boards ----
#include "SdNvsPrefs.h"   // NOT raw NVS Preferences: the touch firmware abandoned NVS (tiny, shared,
                         // doesn't survive Launcher) for a file-backed store. MQTT was the last setting
                         // still on NVS, so its writes silently failed / didn't persist (GH #128).
#include <WiFi.h>
#include <stdio.h>
#include <string.h>
#include <lwip/sockets.h>
#include "esp_random.h"
#include "mbedtls/gcm.h"
#include "mbedtls/md.h"
#include "mbedtls/base64.h"
#include "../../MyMesh.h"

// Only hand bytes to lwIP when the socket can accept them right now (zero-timeout
// select). Otherwise WiFiClient::write() blocks in 1 s select() retries against a
// broker that stopped ACKing — on the loop thread that is a visible UI freeze per
// publish. A refused write fails the publish; PubSubClient then flags the
// connection down and the (async) reconnect path takes over.
size_t MqttNbClient::write(const uint8_t* buf, size_t size) {
    int fd_ = fd();
    if (fd_ < 0) return 0;
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(fd_, &wset);
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    if (select(fd_ + 1, NULL, &wset, NULL, &tv) <= 0 || !FD_ISSET(fd_, &wset)) return 0;
    return WiFiClient::write(buf, size);
}

void MqttBridge::getStatus(MqttBridgeStatus& out) const {
    portENTER_CRITICAL(&_statusMux);
    out = _status;
    portEXIT_CRITICAL(&_statusMux);
}

void MqttBridge::syncStatusConfig(MqttBridgePhase phase) {
    MqttBridgeStatus next;
    next.phase = phase;
    next.available = true;
    next.requestedEnabled = _requestedEnabled;
    next.publishDm = _pubDm;
    next.publishChannel = _pubChannel;
    next.encrypted = _encOn;
    strncpy(next.host, _host, sizeof(next.host) - 1);
    next.port = _port;
    portENTER_CRITICAL(&_statusMux);
    _status = next;
    portEXIT_CRITICAL(&_statusMux);
}

void MqttBridge::setStatusPhase(MqttBridgePhase phase, int result, uint32_t now,
                                bool recordResult, uint32_t retryAt) {
    portENTER_CRITICAL(&_statusMux);
    _status.phase = phase;
    _status.retryAtMs = retryAt;
    if (phase == MqttBridgePhase::Connecting) _status.lastAttemptMs = now;
    if (phase == MqttBridgePhase::Connected)  _status.lastConnectedMs = now;
    if (recordResult) {
        _status.lastResult = (int8_t)result;
        _status.lastResultMs = now;
    }
    portEXIT_CRITICAL(&_statusMux);
}

// Read every field from the "mqtt" NVS namespace into the members. isKey() guards
// avoid the [E] NOT_FOUND log spam that getString emits for absent keys on the
// USB-CDC companion stream (see TouchPrefsStore prefsGetStr note).
void MqttBridge::loadConfig() {
    _requestedEnabled = false; _enabled = false; _pubDm = false; _pubChannel = true;
    _host[0] = _user[0] = _pwd[0] = _psk[0] = '\0';
    _port = 1883;

    SdNvsPrefs p;
    bool hasHost = false;
    bool consent = false;
    if (p.begin("mqtt", true)) {
        hasHost           = p.isKey("host");
        consent           = p.getBool("consent", false);
        _requestedEnabled = consent && p.getBool("en", false);
        _port             = (uint16_t)p.getUInt("port", 1883);
        _pubDm            = consent && p.getBool("dm", false);
        _pubChannel       = p.getBool("ch", true);
        if (hasHost)         p.getString("host", _host, sizeof(_host));
        if (p.isKey("user")) p.getString("user", _user, sizeof(_user));
        if (p.isKey("pwd"))  p.getString("pwd",  _pwd,  sizeof(_pwd));
        if (p.isKey("psk"))  p.getString("psk",  _psk,  sizeof(_psk));
        p.end();
    }
    deriveKey();
    _enabled = _requestedEnabled && _host[0] != '\0';
    MqttBridgePhase phase = MqttBridgePhase::Disabled;
    if (_requestedEnabled && _host[0] == '\0') phase = MqttBridgePhase::Misconfigured;
    else if (_enabled) phase = WiFi.status() == WL_CONNECTED
                                ? MqttBridgePhase::RetryWait
                                : MqttBridgePhase::WaitingForWifi;
    syncStatusConfig(phase);
}

// Derive the AES key from the passphrase. mbedtls_md() (generic digest) is used
// rather than mbedtls_sha256(), whose _ret suffix differs across mbedTLS 2.x/3.x.
void MqttBridge::deriveKey() {
    _encOn = (_psk[0] != '\0');
    if (!_encOn) { memset(_key, 0, sizeof(_key)); return; }
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!info || mbedtls_md(info, (const unsigned char*)_psk, strlen(_psk), _key) != 0) {
        memset(_key, 0, sizeof(_key));
        _encOn = false;   // can't derive → fail closed to plaintext-disabled publishing
    }
}

void MqttBridge::begin(const char* nodeHex) {
    strncpy(_nodeHex, nodeHex, sizeof(_nodeHex) - 1);
    _nodeHex[sizeof(_nodeHex) - 1] = '\0';

    loadConfig();
    if (!_enabled) return;

    _mqtt.setServer(_host, _port);
    _mqtt.setCallback(MqttBridge::mqttCallback);
    _mqtt.setKeepAlive(60);
    _mqtt.setSocketTimeout(2);                  // bound connect/read — a dead broker must not stall loop()
    _mqtt.setBufferSize(_encOn ? 1024 : 768);   // sealed base64 (or 576B TX DM JSON) needs the bigger buffer
    Serial.printf("[MQTT] configured -> %s:%u dm=%d ch=%d enc=%d\n",
                  _host, _port, (int)_pubDm, (int)_pubChannel, (int)_encOn);
}

bool MqttBridge::reconnect() {
    if (!_enabled) return false;
    if (WiFi.status() != WL_CONNECTED) {
        setStatusPhase(MqttBridgePhase::WaitingForWifi, MQTT_DISCONNECTED,
                       millis(), false);
        return false;
    }

    char clientId[32], lwtTopic[80];
    snprintf(clientId, sizeof(clientId), "wadamesh-%s", _nodeHex);
    snprintf(lwtTopic, sizeof(lwtTopic), "wadamesh/%s/status", _nodeHex);

    bool ok = _user[0]
        ? _mqtt.connect(clientId, _user, _pwd, lwtTopic, 0, true, "offline")
        : _mqtt.connect(clientId, nullptr, nullptr, lwtTopic, 0, true, "offline");

    const uint32_t now = millis();
    const int result = _mqtt.state();
    if (ok) {
        setStatusPhase(MqttBridgePhase::Connected, result, now, true);
        _mqtt.publish(lwtTopic, "online", true);
        if (_pubDm && _encOn && _nodeHex[0]) {
            char rxTopic[80];
            snprintf(rxTopic, sizeof(rxTopic), "wadamesh/%s/rx/dm", _nodeHex);
            _mqtt.subscribe(rxTopic);
        }
        Serial.printf("[MQTT] connected as %s\n", clientId);
    } else {
        MqttBridgeStatus current;
        getStatus(current);
        const uint32_t retryAt = current.lastAttemptMs
                                   ? current.lastAttemptMs + RECONNECT_INTERVAL_MS
                                   : now + RECONNECT_INTERVAL_MS;
        setStatusPhase(MqttBridgePhase::RetryWait, result, now, true,
                       retryAt);
        Serial.printf("[MQTT] connect failed, rc=%d\n", result);
    }
    return ok;
}

// reconnect() blocks on DNS + TCP connect + CONNACK (2-10 s against an unreachable
// broker) — never run it on the loop thread, it drives LVGL. One-shot task instead;
// _connecting hands _mqtt/_wc to the task and back.
void MqttBridge::reconnectTask(void* arg) {
    MqttBridge* self = (MqttBridge*)arg;
    self->reconnect();
    self->_connecting = false;
    vTaskDelete(nullptr);
}

void MqttBridge::loop() {
    if (!_enabled || _connecting) return;
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
        if (_mqtt.connected()) _mqtt.loop();
        setStatusPhase(MqttBridgePhase::WaitingForWifi, MQTT_DISCONNECTED,
                       now, false);
        return;
    }
    if (!_mqtt.connected()) {
        MqttBridgeStatus current;
        getStatus(current);
        if (current.phase == MqttBridgePhase::Connected) {
            setStatusPhase(MqttBridgePhase::RetryWait, _mqtt.state(), now, true,
                           now);
        }
        if (_lastReconnectMs == 0 || (uint32_t)(now - _lastReconnectMs) >= RECONNECT_INTERVAL_MS) {
            _lastReconnectMs = now ? now : 1;
            _connecting = true;
            setStatusPhase(MqttBridgePhase::Connecting, MQTT_DISCONNECTED,
                           now, false);
            if (xTaskCreatePinnedToCore(reconnectTask, "mqtt_conn", 6144, this,
                                        1, nullptr, 0) != pdPASS) {
                _connecting = false;   // task OOM — try again next interval
                setStatusPhase(MqttBridgePhase::RetryWait, MQTT_DISCONNECTED,
                               now, false, now + RECONNECT_INTERVAL_MS);
            }
        } else {
            setStatusPhase(MqttBridgePhase::RetryWait, MQTT_DISCONNECTED,
                           now, false, _lastReconnectMs + RECONNECT_INTERVAL_MS);
        }
        return;
    }
    if (!_mqtt.loop()) {
        setStatusPhase(MqttBridgePhase::RetryWait, _mqtt.state(), now, true,
                       now);
    }
}

// AES-256-GCM seal: out = base64( nonce[12] || ciphertext || tag[16] ). Single-task
// use (mesh callbacks + loop() run on the same Arduino task), so the stack buffers
// are safe. Returns false on any crypto/size failure (caller then publishes nothing).
bool MqttBridge::sealToB64(const char* plain, char* out, size_t outCap) {
    static constexpr size_t kMaxPlainLen = 576;
    size_t plen = strlen(plain);
    if (plen > kMaxPlainLen) return false;        // matches the json[] producers below

    uint8_t blob[12 + kMaxPlainLen + 16];
    esp_fill_random(blob, 12);                    // fresh nonce per message
    uint8_t tag[16];

    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_crypt_and_tag(&gcm, MBEDTLS_GCM_ENCRYPT, plen,
                                       blob, 12, nullptr, 0,
                                       (const unsigned char*)plain, blob + 12,
                                       16, tag);
    }
    mbedtls_gcm_free(&gcm);
    if (rc != 0) return false;

    memcpy(blob + 12 + plen, tag, 16);
    size_t blobLen = 12 + plen + 16, olen = 0;
    return mbedtls_base64_encode((unsigned char*)out, outCap, &olen, blob, blobLen) == 0;
}

void MqttBridge::mqttCallback(char* topic, uint8_t* payload, unsigned int length) {
    mqtt_bridge.onRxPacket(topic, payload, length);
}

bool MqttBridge::parseAuthenticatedRxDm(const char* plain, size_t len,
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

void MqttBridge::onRxPacket(const char* topic, const uint8_t* payload, unsigned int length) {
    if (!_enabled || !_pubDm || !_encOn || !_nodeHex[0] || !topic || !payload) return;
    char expectedTopic[80];
    snprintf(expectedTopic, sizeof(expectedTopic), "wadamesh/%s/rx/dm", _nodeHex);
    if (strcmp(topic, expectedTopic) != 0) return;
    if (length < 28 + 14 || length > 28 + 300) return;
    const size_t clen = length - 28;
    uint8_t plain[304];
    mbedtls_gcm_context gcm;
    mbedtls_gcm_init(&gcm);
    int rc = mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, _key, 256);
    if (rc == 0) {
        rc = mbedtls_gcm_auth_decrypt(&gcm, clen,
                                      payload, 12,
                                      nullptr, 0,
                                      payload + 12, 16,
                                      payload + 28, plain);
    }
    mbedtls_gcm_free(&gcm);
    if (rc != 0) return;
    plain[clen] = '\0';
    uint8_t senderPrefix[6] = {};
    const char* msgText = nullptr;
    if (!parseAuthenticatedRxDm((const char*)plain, clen, senderPrefix, &msgText)) return;
    the_mesh.onMqttDirectMessageRecv(senderPrefix, msgText);
}

void MqttBridge::pub(const char* subtopic, const char* json) {
    if (!_enabled || _connecting || !_mqtt.connected()) return;
    char topic[80];
    snprintf(topic, sizeof(topic), "wadamesh/%s/%s", _nodeHex, subtopic);
    if (_encOn) {
        char b64[832];                            // base64 of the <=604-byte sealed blob
        if (sealToB64(json, b64, sizeof(b64))) _mqtt.publish(topic, b64);
    } else {
        _mqtt.publish(topic, json);
    }
}

void MqttBridge::escapeJson(const char* src, char* dst, size_t dstLen) {
    static const char* hexd = "0123456789abcdef";
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 7 < dstLen; ++i) {   // +7: worst case is \u00XX (6) + NUL
        unsigned char c = (unsigned char)src[i];
        if (c == '"' || c == '\\') { dst[j++] = '\\'; dst[j++] = (char)c; }
        else if (c == '\n')        { dst[j++] = '\\'; dst[j++] = 'n'; }
        else if (c == '\r')        { dst[j++] = '\\'; dst[j++] = 'r'; }
        else if (c == '\t')        { dst[j++] = '\\'; dst[j++] = 't'; }
        else if (c < 0x20) {                              // other control chars → \u00XX (valid JSON)
            dst[j++] = '\\'; dst[j++] = 'u'; dst[j++] = '0'; dst[j++] = '0';
            dst[j++] = hexd[(c >> 4) & 0xF]; dst[j++] = hexd[c & 0xF];
        } else { dst[j++] = (char)c; }
    }
    dst[j] = '\0';
}

void MqttBridge::publishDM(const char* senderName, const uint8_t* senderKey32,
                            float snr, uint8_t hops, uint32_t ts, const char* text,
                            const char* dir, const char* recipientName,
                            const uint8_t* recipientKey32) {
    if (!_enabled || !_pubDm || _connecting || !_mqtt.connected()) return;   // DMs are opt-in
    if (!senderName || !senderKey32 || !text) return;
    char keyHex[13] = {};
    for (int i = 0; i < 6; ++i) snprintf(keyHex + i * 2, 3, "%02x", senderKey32[i]);

    char safeName[48], safeText[300], safeDir[8];
    escapeJson(senderName,       safeName, sizeof(safeName));
    escapeJson(text,             safeText, sizeof(safeText));
    escapeJson(dir ? dir : "rx", safeDir,  sizeof(safeDir));

    char json[576];
    int written = 0;
    if (recipientName && recipientKey32) {
        char rcptKeyHex[13] = {};
        for (int i = 0; i < 6; ++i) snprintf(rcptKeyHex + i * 2, 3, "%02x", recipientKey32[i]);
        char safeRcpt[48];
        escapeJson(recipientName, safeRcpt, sizeof(safeRcpt));
        written = snprintf(json, sizeof(json),
            "{\"dir\":\"%s\",\"sender\":\"%s\",\"key\":\"%s\",\"recipient\":\"%s\",\"recipient_key\":\"%s\",\"snr\":%.1f,\"hops\":%u,\"ts\":%lu,\"text\":\"%s\"}",
            safeDir, safeName, keyHex, safeRcpt, rcptKeyHex, snr, (unsigned)hops, (unsigned long)ts, safeText);
    } else {
        written = snprintf(json, sizeof(json),
            "{\"dir\":\"%s\",\"sender\":\"%s\",\"key\":\"%s\",\"snr\":%.1f,\"hops\":%u,\"ts\":%lu,\"text\":\"%s\"}",
            safeDir, safeName, keyHex, snr, (unsigned)hops, (unsigned long)ts, safeText);
    }
    if (written <= 0 || (size_t)written >= sizeof(json)) return;
    pub("msg/dm", json);
}

void MqttBridge::publishChannel(int channelIdx, const char* channelName,
                                 float snr, uint8_t hops, uint32_t ts, const char* text) {
    if (!_enabled || !_pubChannel || _connecting || !_mqtt.connected()) return;   // channel publish toggle
    char safeName[48], safeText[300];
    escapeJson(channelName, safeName, sizeof(safeName));
    escapeJson(text,        safeText, sizeof(safeText));

    char json[480];
    snprintf(json, sizeof(json),
        "{\"channel\":\"%s\",\"ch_idx\":%d,\"snr\":%.1f,\"hops\":%u,\"ts\":%lu,\"text\":\"%s\"}",
        safeName, channelIdx, snr, (unsigned)hops, (unsigned long)ts, safeText);
    pub("msg/ch", json);
}

void MqttBridge::saveConfig(const char* host, uint16_t port,
                             const char* user, const char* pwd,
                             bool pubDm, bool pubChannel, const char* psk, bool enable) {
    SdNvsPrefs p;
    if (!p.begin("mqtt", false)) return;
    p.putBool("en",     enable);
    p.putString("host", host);
    p.putUInt("port",   port);
    p.putString("user", user);
    p.putString("pwd",  pwd);
    p.putBool("dm",     pubDm);
    p.putBool("ch",     pubChannel);
    p.putString("psk",  psk);
    p.end();
}

void MqttBridge::reloadConfig() {
    // A connect attempt may be in flight on the one-shot task; PubSubClient is not
    // thread-safe, so wait it out (bounded: DNS + TCP + CONNACK <= ~10 s, and it
    // only overlaps when Save lands inside an attempt window on a dead broker).
    while (_connecting) delay(10);
    if (_mqtt.connected()) _mqtt.disconnect();
    loadConfig();
    if (!_enabled) { Serial.println("[MQTT] disabled"); return; }
    _mqtt.setServer(_host, _port);
    _mqtt.setCallback(MqttBridge::mqttCallback);
    _mqtt.setKeepAlive(60);
    _mqtt.setSocketTimeout(2);
    _mqtt.setBufferSize(_encOn ? 1024 : 768);
    _lastReconnectMs = 0;   // reconnect on next loop() tick
    Serial.printf("[MQTT] reloaded -> %s:%u en=%d dm=%d ch=%d enc=%d\n",
                  _host, _port, (int)_enabled, (int)_pubDm, (int)_pubChannel, (int)_encOn);
}

#endif // !HAS_TANMATSU (real implementation)
#endif // ESP32 && MULTI_TRANSPORT_COMPANION
