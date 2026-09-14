#include <WiFi.h>
#include <Wire.h>
#include <TCPSocket.h>
#include "mbedtls/md.h"

#include "camera.h"
#include "gc2145.h"
#include "secrets.h"

#ifndef ARDUINO_NICLA_VISION
#error "Select Arduino Nicla Vision (M7, Arduino Mbed OS Nicla Boards)."
#endif

#ifndef AUTH_SHARED_KEY
#error "Add AUTH_SHARED_KEY to secrets.h (see secrets.h.example)."
#endif

// SIX_Q Vision (V4): framed I2C protocol (SOF/VER/CMD/SEQ/LEN/
// PAYLOAD/CRC16-CCITT), HMAC-SHA256 challenge-response AUTH, and SEND_TILE.
// UNO Q requests one 320x320 tile, receives it, waits for READY, and only
// then requests the next tile. This restores the pacing validated in V2.
constexpr uint8_t FRAME_SOF = 0xA5;
constexpr uint8_t PROTOCOL_VERSION = 5;
constexpr uint8_t CMD_AUTH_CHALLENGE = 0x01; // UNO Q -> Nicla, payload: nonce[8]
constexpr uint8_t CMD_AUTH_RESPONSE  = 0x02; // Nicla -> UNO Q, payload: mac[16]
constexpr uint8_t CMD_AUTH_ACK       = 0x03; // UNO Q -> Nicla, payload: ok[1]
constexpr uint8_t CMD_SEND_ALL       = 0x10; // reserved; rejected in V4
constexpr uint8_t CMD_SEND_TILE      = 0x11; // payload: capture_id[4]+capture_seq[1]+tile[1]+x[2]+y[2]
constexpr uint8_t CMD_SEND_LIST      = 0x12; // reserved; rejected in V4
constexpr uint8_t CMD_STATUS         = 0xF0; // Nicla -> UNO Q, payload: state[1]+last_result[1]+protocol[1]
constexpr uint8_t MAX_INCOMING_PAYLOAD = 48;
constexpr uint8_t RESPONSE_PAYLOAD_CAP = 16; // fits AUTH_RESPONSE's 16-byte MAC
constexpr uint8_t RESPONSE_FRAME_SIZE = 5 + RESPONSE_PAYLOAD_CAP + 2; // 23, fixed-size I2C reads

constexpr char AUTH_CONTEXT[] = "SIXQ_V4";

enum State : uint8_t {
    ST_BOOT = 0, ST_AUTH = 1, ST_READY = 2, ST_CAPTURE = 3, ST_SEND = 4,
    ST_AUTH_REJECTED = 5,
    ST_CAMERA_INIT_ERROR = 225, ST_TCP_CONNECT_ERROR = 226, ST_WIFI_ERROR = 227,
    ST_ZOOM_ERROR = 228, ST_CAPTURE_ERROR = 231, ST_BUFFER_ERROR = 232,
    ST_TCP_WRITE_ERROR = 233, ST_EXPOSURE_ERROR = 234, ST_NETWORK_CONFIG_ERROR = 235
};
enum LastResult : uint8_t {
    RES_OK = 0, RES_BUSY = 1, RES_INVALID = 2, RES_CRC_ERROR = 3, RES_NOT_IMPLEMENTED = 4
};
constexpr uint8_t SEND_OK = 0xFE; // sentinel distinct from every State value

// 320x320 / 5x4 grid, unchanged from the validated Windowing_TEST V2 baseline.
constexpr uint16_t FULL_WIDTH = 1600, FULL_HEIGHT = 1200;
constexpr uint16_t TILE_WIDTH = 320, TILE_HEIGHT = 320;
constexpr uint16_t LAST_X = FULL_WIDTH - TILE_WIDTH, LAST_Y = FULL_HEIGHT - TILE_HEIGHT;
constexpr uint8_t GRID_COLS = 5, GRID_ROWS = 4;
constexpr uint8_t TILE_COUNT = GRID_COLS * GRID_ROWS; // 20
constexpr uint16_t GRID_X[GRID_COLS] = {0, 320, 640, 960, 1280};
constexpr uint16_t GRID_Y[GRID_ROWS] = {0, 320, 640, 880};
constexpr size_t FRAME_BYTES = TILE_WIDTH * TILE_HEIGHT * 2;
constexpr uint32_t FRAME_TIMEOUT_MS = 6000;
constexpr uint32_t TCP_TIMEOUT_MS = 12000;
constexpr uint8_t WARMUP_DISCARD_FRAMES = 2;
constexpr uint32_t SERIAL_ATTACH_WAIT_MS = 5000;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 2000;

GC2145 galaxyCore;
Camera cam(galaxyCore);
FrameBuffer frameBuffer(TILE_WIDTH, TILE_HEIGHT, 2);
bool cameraReady = false;

struct SendTileRequest {
    uint32_t captureId;
    uint8_t captureSeq;
    uint8_t tileId;
    uint16_t x;
    uint16_t y;
};

volatile uint8_t niclaState = ST_BOOT;
volatile uint8_t lastResult = RES_OK;
volatile uint8_t lastSeq = 0;
volatile bool busy = false;
volatile bool authChallengePending = false;
volatile bool authAcceptedPending = false;
volatile bool sendTilePending = false;
uint8_t pendingNonce[8] = {};
SendTileRequest pendingSendTile = {};
uint8_t responseFrame[RESPONSE_FRAME_SIZE] = {};
uint32_t lastHeartbeatMs = 0;

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i] << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

uint16_t read16(const uint8_t* data) {
    return ((uint16_t)data[0] << 8) | data[1];
}

uint32_t read32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

void buildFrame(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen) {
    // Wire callbacks run in an Mbed thread. Publish the whole frame before
    // onRequest can copy it, including its CRC.
    core_util_critical_section_enter();
    memset(responseFrame, 0, sizeof(responseFrame));
    responseFrame[0] = FRAME_SOF;
    responseFrame[1] = PROTOCOL_VERSION;
    responseFrame[2] = cmd;
    responseFrame[3] = lastSeq;
    responseFrame[4] = payloadLen;
    if (payload && payloadLen) memcpy(responseFrame + 5, payload, payloadLen);
    const uint16_t crc = crc16_ccitt(responseFrame, 5 + payloadLen);
    responseFrame[5 + payloadLen] = (uint8_t)(crc >> 8);
    responseFrame[6 + payloadLen] = (uint8_t)(crc & 0xff);
    core_util_critical_section_exit();
}

void publishStatus() {
    const uint8_t payload[3] = {niclaState, lastResult, PROTOCOL_VERSION};
    buildFrame(CMD_STATUS, payload, sizeof(payload));
}

// HMAC-SHA256(AUTH_SHARED_KEY, nonce + AUTH_CONTEXT), truncated to 16 bytes.
void computeAuthResponse(const uint8_t* nonce, uint8_t* macOut16) {
    uint8_t message[8 + sizeof(AUTH_CONTEXT) - 1];
    memcpy(message, nonce, 8);
    memcpy(message + 8, AUTH_CONTEXT, sizeof(AUTH_CONTEXT) - 1);
    uint8_t fullMac[32];
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    mbedtls_md_hmac(info, (const unsigned char*)AUTH_SHARED_KEY, strlen(AUTH_SHARED_KEY),
                     message, sizeof(message), fullMac);
    memcpy(macOut16, fullMac, 16);
}

void onReceive(int count) {
    uint8_t raw[5 + MAX_INCOMING_PAYLOAD + 2];
    size_t length = 0;
    while (Wire.available()) {
        int value = Wire.read();
        if (length < sizeof(raw)) raw[length++] = (uint8_t)value;
    }
    if (length < 7 || raw[0] != FRAME_SOF || raw[1] != PROTOCOL_VERSION) {
        lastResult = RES_INVALID; publishStatus(); return;
    }
    const uint8_t cmd = raw[2];
    lastSeq = raw[3];
    const uint8_t payloadLen = raw[4];
    if (length != (size_t)(7 + payloadLen)) {
        lastResult = RES_INVALID; publishStatus(); return;
    }
    const uint16_t receivedCrc = ((uint16_t)raw[5 + payloadLen] << 8) | raw[6 + payloadLen];
    if (crc16_ccitt(raw, 5 + payloadLen) != receivedCrc) {
        lastResult = RES_CRC_ERROR; publishStatus(); return;
    }
    const uint8_t* payload = raw + 5;

    const bool capturePhaseCommand = cmd == CMD_AUTH_CHALLENGE || cmd == CMD_AUTH_ACK ||
        cmd == CMD_SEND_ALL || cmd == CMD_SEND_TILE || cmd == CMD_SEND_LIST;
    if (busy && capturePhaseCommand) {
        lastResult = RES_BUSY; publishStatus(); return;
    }

    switch (cmd) {
        case CMD_AUTH_CHALLENGE:
            if (payloadLen != 8) { lastResult = RES_INVALID; publishStatus(); break; }
            memcpy(pendingNonce, payload, 8);
            authChallengePending = true;
            busy = true;
            niclaState = ST_AUTH;
            lastResult = RES_OK;
            publishStatus(); // AUTH_RESPONSE is built in loop(); poll again to fetch it
            break;
        case CMD_AUTH_ACK:
            if (payloadLen != 1 || niclaState != ST_AUTH) {
                lastResult = RES_INVALID; publishStatus(); break;
            }
            if (payload[0]) {
                authAcceptedPending = true; // camera/WiFi init runs in loop(), not here
                busy = true;
            } else {
                niclaState = ST_AUTH_REJECTED;
            }
            lastResult = RES_OK;
            publishStatus();
            break;
        case CMD_SEND_TILE:
            if (payloadLen != 10 || niclaState != ST_READY) {
                lastResult = RES_INVALID; publishStatus(); break;
            }
            pendingSendTile.captureId = read32(payload);
            pendingSendTile.captureSeq = payload[4];
            pendingSendTile.tileId = payload[5];
            pendingSendTile.x = read16(payload + 6);
            pendingSendTile.y = read16(payload + 8);
            if (pendingSendTile.captureId == 0 || pendingSendTile.tileId >= TILE_COUNT ||
                pendingSendTile.x != GRID_X[pendingSendTile.tileId % GRID_COLS] ||
                pendingSendTile.y != GRID_Y[pendingSendTile.tileId / GRID_COLS]) {
                lastResult = RES_INVALID; publishStatus(); break;
            }
            sendTilePending = true;
            busy = true;
            niclaState = ST_CAPTURE;
            lastResult = RES_OK;
            publishStatus();
            break;
        case CMD_SEND_ALL:
        case CMD_SEND_LIST:
            lastResult = RES_NOT_IMPLEMENTED;
            publishStatus();
            break;
        default:
            lastResult = RES_INVALID;
            publishStatus();
            break;
    }
}

void onRequest() {
    core_util_critical_section_enter();
    Wire.write(responseFrame, RESPONSE_FRAME_SIZE);
    core_util_critical_section_exit();
}

bool validNetwork() {
    return WIFI_SSID[0] != '\0' && WIFI_PASSWORD[0] != '\0' &&
           UNO_Q_IP_1 > 0 && UNO_Q_IP_1 < 224 && UNO_Q_IP_1 != 127 &&
           UNO_Q_IP_2 <= 255 && UNO_Q_IP_3 <= 255 && UNO_Q_IP_4 <= 255 &&
           UNO_Q_PORT > 0 && UNO_Q_PORT <= 65535;
}

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;
    WiFi.setTimeout(12000);
    for (int attempt = 0; attempt < 3; ++attempt) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        if (WiFi.status() == WL_CONNECTED) {
            Serial.print("NICLA | IP = ");
            Serial.println(WiFi.localIP());
            return true;
        }
        WiFi.disconnect();
        delay(500);
    }
    return false;
}

bool sensorWrite(uint8_t reg, uint8_t value) {
    CameraWire.beginTransmission(GC2145_I2C_ADDR);
    CameraWire.write(reg);
    CameraWire.write(value);
    return CameraWire.endTransmission() == 0;
}

bool sensorRead(uint8_t reg, uint8_t& value) {
    CameraWire.beginTransmission(GC2145_I2C_ADDR);
    CameraWire.write(reg);
    if (CameraWire.endTransmission(false) != 0 ||
        CameraWire.requestFrom((uint8_t)GC2145_I2C_ADDR, (uint8_t)1) != 1)
        return false;
    value = (uint8_t)CameraWire.read();
    return true;
}

bool sensorAutoControl(uint8_t reg, uint8_t mask, bool enabled) {
    uint8_t value = 0;
    if (!sensorWrite(0xfe, 0) || !sensorRead(reg, value)) return false;
    return sensorWrite(reg, enabled ? (value | mask) : (value & ~mask));
}

// GC2145 datasheet V1.0: P0:b6 bit0 AEC; P0:82 bit1 AWB.
bool setAutomaticImageControls(bool enabled) {
    return sensorAutoControl(0xb6, 0x01, enabled) &&
           sensorAutoControl(0x82, 0x02, enabled);
}

bool configureTile(uint16_t x, uint16_t y) {
    if (x > LAST_X || y > LAST_Y) return false;
    if (cam.zoomTo(CAMERA_R320x320, 0, 0) != 0) return false;
    if (!sensorWrite(0xfe, 0) || !sensorWrite(0x90, 1)) return false;
    if (!sensorWrite(0x91, y >> 8) || !sensorWrite(0x92, y & 0xff) ||
        !sensorWrite(0x93, x >> 8) || !sensorWrite(0x94, x & 0xff)) return false;
    uint8_t yHigh = 0, yLow = 0, xHigh = 0, xLow = 0;
    return sensorRead(0x91, yHigh) && yHigh == (uint8_t)(y >> 8) &&
           sensorRead(0x92, yLow) && yLow == (uint8_t)y &&
           sensorRead(0x93, xHigh) && xHigh == (uint8_t)(x >> 8) &&
           sensorRead(0x94, xLow) && xLow == (uint8_t)x;
}

bool frameBufferValid() {
    return frameBuffer.getBuffer() != nullptr &&
           ((uintptr_t)frameBuffer.getBuffer() & 31U) == 0 &&
           frameBuffer.getBufferSize() >= FRAME_BYTES &&
           cam.frameSize() == (int)FRAME_BYTES;
}

int captureFrame() {
    if (!frameBufferValid()) return -1;
    SCB_CleanInvalidateDCache_by_Addr((uint32_t*)frameBuffer.getBuffer(), FRAME_BYTES);
    return cam.grabFrame(frameBuffer, FRAME_TIMEOUT_MS);
}

int captureAfterDiscard() {
    for (uint8_t discarded = 0; discarded < WARMUP_DISCARD_FRAMES; ++discarded) {
        if (captureFrame() != 0) return -1;
    }
    return captureFrame();
}

uint8_t prepareCamera() {
    if (!validNetwork()) return ST_NETWORK_CONFIG_ERROR;
    if (!cameraReady) {
        Serial.println("NICLA | CAMERA INITIALIZING");
        cam.debug(Serial);
        cameraReady = cam.begin(CAMERA_R1600x1200, CAMERA_RGB565, 30);
        if (!cameraReady) return ST_CAMERA_INIT_ERROR;
        Serial.println("NICLA | CAMERA INITIALIZED");
        if (cam.setVerticalFlip(true) != 0 || cam.setHorizontalMirror(true) != 0)
            return ST_CAMERA_INIT_ERROR;
    }
    Serial.println("NICLA | WIFI CONNECTING");
    if (!connectWiFi()) return ST_WIFI_ERROR;
    Serial.println("NICLA | TILE CONFIGURING");
    if (!configureTile(0, 0) || !frameBufferValid()) return ST_BUFFER_ERROR;
    if (!setAutomaticImageControls(true)) return ST_EXPOSURE_ERROR;
    Serial.println("NICLA | EXPOSURE WARMUP");
    delay(1500);
    if (captureAfterDiscard() != 0) return ST_CAPTURE_ERROR;
    if (!setAutomaticImageControls(false)) return ST_EXPOSURE_ERROR;
    return ST_READY;
}

bool sendAll(TCPSocket& socket, const uint8_t* data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        nsapi_size_or_error_t sent = socket.send(data + offset, length - offset > 1024 ? 1024 : length - offset);
        if (sent <= 0) return false;
        offset += (size_t)sent;
    }
    return true;
}

// send_duration_ms = capture-to-TCP-connected latency (knowable before the
// header is written; Nicla never needs absolute time, only millis() deltas).
uint8_t sendTileOverTcp(uint32_t captureId, uint8_t captureSeq, uint8_t tileId,
                        uint16_t x, uint16_t y, uint32_t capturedMs) {
    if (WiFi.status() != WL_CONNECTED) return ST_WIFI_ERROR;
    char address[16];
    snprintf(address, sizeof(address), "%d.%d.%d.%d", UNO_Q_IP_1, UNO_Q_IP_2, UNO_Q_IP_3, UNO_Q_IP_4);
    SocketAddress destination;
    if (!destination.set_ip_address(address)) return ST_NETWORK_CONFIG_ERROR;
    destination.set_port(UNO_Q_PORT);
    TCPSocket socket;
    if (socket.open(WiFi.getNetwork()) != NSAPI_ERROR_OK) return ST_TCP_CONNECT_ERROR;
    socket.set_timeout(TCP_TIMEOUT_MS);
    if (socket.connect(destination) != NSAPI_ERROR_OK) {
        socket.close();
        return ST_TCP_CONNECT_ERROR;
    }
    const uint32_t sendDurationMs = millis() - capturedMs;
    char header[192];
    int length = snprintf(header, sizeof(header),
        "SIXQ_TILE_V4\nRGB565\n%u\n%u\n%lu\n%lu\n%u\n%u\n%u\n%u\n%lu\n%lu\n\n",
        TILE_WIDTH, TILE_HEIGHT, (unsigned long)FRAME_BYTES,
        (unsigned long)captureId, captureSeq, tileId, x, y,
        (unsigned long)capturedMs, (unsigned long)sendDurationMs);
    const bool sent = length > 0 && length < (int)sizeof(header) &&
        sendAll(socket, (const uint8_t*)header, length) &&
        sendAll(socket, frameBuffer.getBuffer(), FRAME_BYTES);
    socket.close();
    return sent ? SEND_OK : ST_TCP_WRITE_ERROR;
}

void runSendTile(const SendTileRequest& request) {
    niclaState = ST_CAPTURE; lastResult = RES_OK; publishStatus();
    if (!configureTile(request.x, request.y) || !frameBufferValid()) {
        niclaState = ST_ZOOM_ERROR; publishStatus(); busy = false; return;
    }
    if (captureAfterDiscard() != 0) {
        niclaState = ST_CAPTURE_ERROR; publishStatus(); busy = false; return;
    }
    const uint32_t capturedMs = millis();
    niclaState = ST_SEND; publishStatus();
    const uint8_t result = sendTileOverTcp(request.captureId, request.captureSeq,
                                           request.tileId, request.x, request.y, capturedMs);
    niclaState = result == SEND_OK ? ST_READY : result;
    lastResult = RES_OK;
    busy = false;
    publishStatus();
}

void printHeartbeat() {
    const uint32_t now = millis();
    if (now - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
    lastHeartbeatMs = now;
    Serial.print("NICLA | HEARTBEAT | UPTIME_MS=");
    Serial.print(now);
    Serial.print(" | STATE=");
    Serial.print(niclaState);
    Serial.print(" | RESULT=");
    Serial.println(lastResult);
}

void setup() {
    Serial.begin(115200);
    pinMode(PB_8, INPUT_PULLUP);        //Aportan estabilidad a la comunicación I2C
    pinMode(PB_9, INPUT_PULLUP);        //Aportan estabilidad a la comunicación I2C
  
    Wire.begin(0x42);
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
    niclaState = ST_BOOT;
    publishStatus();

    // Arduino Cloud/Web Monitor can attach after setup has already started.
    // Keep I2C active above, then wait only a bounded time for USB serial so
    // the boot line is visible without ever blocking headless operation.
    const uint32_t serialWaitStartedMs = millis();
    while (!Serial && millis() - serialWaitStartedMs < SERIAL_ATTACH_WAIT_MS) {
        delay(10);
    }
    Serial.println("NICLA | SIX_Q VISION V4 | SEND_TILE | WAITING FOR AUTH_CHALLENGE");
    Serial.print("NICLA | SERIAL ATTACHED = ");
    Serial.println(Serial ? "YES" : "NO (continuing headless)");
    lastHeartbeatMs = millis();
}

void loop() {
    if (authChallengePending) {
        authChallengePending = false;
        uint8_t mac[16];
        computeAuthResponse(pendingNonce, mac);
        buildFrame(CMD_AUTH_RESPONSE, mac, sizeof(mac));
        busy = false;
    } else if (authAcceptedPending) {
        authAcceptedPending = false;
        Serial.println("NICLA | AUTH ACK ACCEPTED | STARTING WARMUP");
        const uint8_t result = prepareCamera();
        niclaState = result; lastResult = RES_OK; busy = false; publishStatus();
        Serial.print("NICLA | WARMUP RESULT = ");
        Serial.println(result);
    } else if (sendTilePending) {
        sendTilePending = false;
        const SendTileRequest request = pendingSendTile;
        runSendTile(request);
    }
    printHeartbeat();
    delay(5);
}

