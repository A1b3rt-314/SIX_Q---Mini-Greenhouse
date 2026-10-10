// SIX_Q V7: fixed GC2145 output, DCMI tile crop, guarded DMA completion.
#include <WiFi.h>
#include <Wire.h>
#include <TCPSocket.h>
#include "mbedtls/md.h"

#include "camera.h"
#include "gc2145.h"
#include "secrets.h"
#include "stm32h7xx_hal.h"
#if (USE_HAL_DCMI_REGISTER_CALLBACKS == 1)
#error "V7 requires default weak DCMI callbacks; validate other cores separately."
#endif

#ifndef ARDUINO_NICLA_VISION
#error "Select Arduino Nicla Vision (M7, Arduino Mbed OS Nicla Boards)."
#endif

#ifndef AUTH_SHARED_KEY
#error "Add AUTH_SHARED_KEY to secrets.h (see secrets.h.example)."
#endif

// ============================================================
// Protocolo I2C (UNO Q <-> Nicla)
// ============================================================
// Revision 9 requires V7 peers. The 13-byte SEND_TILE request is retained.
constexpr uint8_t FRAME_SOF = 0xA5;
constexpr uint8_t PROTOCOL_VERSION = 9;
constexpr uint8_t CMD_AUTH_CHALLENGE = 0x01; // UNO Q -> Nicla, payload: nonce[8]
constexpr uint8_t CMD_AUTH_RESPONSE  = 0x02; // Nicla -> UNO Q, payload: mac[16]
constexpr uint8_t CMD_AUTH_ACK       = 0x03; // UNO Q -> Nicla, payload: ok[1]
constexpr uint8_t CMD_SEND_TILE      = 0x11; // capture_id[4]+seq[1]+tile[1]+x[2]+y[2]+ordinal[1]+reinforced[1] = 13
constexpr uint8_t CMD_SET_DIAGNOSTIC = 0x13; // payload: enabled[1]
constexpr uint8_t CMD_STATUS         = 0xF0; // state[1], last_result[1], wire_revision[1], diagnostic_enabled[1]
constexpr uint8_t MAX_INCOMING_PAYLOAD = 48;
constexpr uint8_t RESPONSE_PAYLOAD_CAP = 16; // fits AUTH_RESPONSE's 16-byte MAC
constexpr uint8_t RESPONSE_FRAME_SIZE = 5 + RESPONSE_PAYLOAD_CAP + 2; // 23, fixed-size I2C reads

constexpr char AUTH_CONTEXT[] = "SIXQ_V4"; // kept identical on purpose: shared-key agreement, not a version label

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
constexpr uint8_t SEND_OK = 0xFE;

// ============================================================
// Geometría del mosaico
// ============================================================
constexpr uint16_t FULL_WIDTH = 1600, FULL_HEIGHT = 1200;
constexpr uint16_t TILE_WIDTH = 320, TILE_HEIGHT = 320;
constexpr uint16_t WIDE_WIDTH = 384;
constexpr size_t WIDE_BYTES = size_t(WIDE_WIDTH) * TILE_HEIGHT * 2U;
constexpr uint16_t LAST_X = FULL_WIDTH - TILE_WIDTH, LAST_Y = FULL_HEIGHT - TILE_HEIGHT;
constexpr uint8_t GRID_COLS = 5, GRID_ROWS = 4;
constexpr uint8_t TILE_COUNT = GRID_COLS * GRID_ROWS; // 20
constexpr uint16_t GRID_X[GRID_COLS] = {0, 320, 640, 960, 1280};
constexpr uint16_t GRID_Y[GRID_ROWS] = {0, 320, 640, 880};
constexpr size_t FRAME_BYTES = TILE_WIDTH * TILE_HEIGHT * 2;
constexpr uint32_t FRAME_TIMEOUT_MS = 6000;
constexpr uint32_t TCP_TIMEOUT_MS = 13000;
constexpr uint32_t SERIAL_ATTACH_WAIT_MS = 5000;
constexpr uint32_t HEARTBEAT_INTERVAL_MS = 2000;

// ============================================================
// Parámetros de captura — sensor fijo; el perfil reforzado usa sobremargen
// ============================================================
constexpr uint8_t DISCARD_FRAMES_NORMAL = 0;
constexpr uint8_t DISCARD_FRAMES_REINFORCED = 1;

GC2145 galaxyCore;
Camera cam(galaxyCore);
FrameBuffer frameBuffer(WIDE_WIDTH, TILE_HEIGHT, 2);
bool cameraReady = false;

struct SendTileRequest {
    uint32_t captureId;
    uint8_t captureSeq;
    uint8_t tileId;
    uint16_t x;
    uint16_t y;
    uint8_t requestOrdinal;
    bool reinforced;
    uint8_t attempt;
};

volatile uint8_t niclaState = ST_BOOT;
volatile uint8_t lastResult = RES_OK;
volatile uint8_t lastSeq = 0;
volatile bool busy = false;
volatile bool authChallengePending = false;
volatile bool authAcceptedPending = false;
volatile bool sendTilePending = false;
volatile bool diagnosticEnabled = false;
volatile bool diagnosticModeChangedPending = false;
uint8_t pendingNonce[8] = {};
SendTileRequest pendingSendTile = {};
uint8_t responseFrame[RESPONSE_FRAME_SIZE] = {};
uint32_t lastHeartbeatMs = 0;

// ============================================================
// CRC
// ============================================================
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

// CRC-32/ISO-HDLC, idéntico a zlib.crc32() del lado Python.
uint32_t crc32_ieee(const uint8_t* data, size_t len) {
    uint32_t crc = 0xFFFFFFFFUL;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

uint16_t read16(const uint8_t* data) { return ((uint16_t)data[0] << 8) | data[1]; }
uint32_t read32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
}

void buildFrame(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen) {
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
    const uint8_t payload[4] = {niclaState, lastResult, PROTOCOL_VERSION, diagnosticEnabled ? (uint8_t)1 : (uint8_t)0};
    buildFrame(CMD_STATUS, payload, sizeof(payload));
}

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

struct ControlGuard {
    ControlGuard() { core_util_critical_section_enter(); }
    ~ControlGuard() { core_util_critical_section_exit(); }
};

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
    ControlGuard guard;

    const bool capturePhaseCommand = cmd == CMD_AUTH_CHALLENGE || cmd == CMD_AUTH_ACK || cmd == CMD_SEND_TILE || cmd == CMD_SET_DIAGNOSTIC;
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
            publishStatus();
            break;
        case CMD_AUTH_ACK:
            if (payloadLen != 1 || niclaState != ST_AUTH) {
                lastResult = RES_INVALID; publishStatus(); break;
            }
            if (payload[0]) {
                authAcceptedPending = true;
                busy = true;
            } else {
                niclaState = ST_AUTH_REJECTED;
            }
            lastResult = RES_OK;
            publishStatus();
            break;
        case CMD_SEND_TILE:
            if (payloadLen != 13 || niclaState != ST_READY) {
                lastResult = RES_INVALID; publishStatus(); break;
            }
            pendingSendTile.captureId = read32(payload);
            pendingSendTile.captureSeq = payload[4];
            pendingSendTile.tileId = payload[5];
            pendingSendTile.x = read16(payload + 6);
            pendingSendTile.y = read16(payload + 8);
            pendingSendTile.requestOrdinal = payload[10];
            pendingSendTile.reinforced = payload[11] != 0;
            pendingSendTile.attempt = payload[12];
            if (pendingSendTile.captureId == 0 || payload[11] > 1 || pendingSendTile.attempt > 5 || pendingSendTile.tileId >= TILE_COUNT ||
                pendingSendTile.requestOrdinal >= TILE_COUNT ||
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
        case CMD_SET_DIAGNOSTIC:
            if (payloadLen != 1 || niclaState != ST_READY || payload[0] > 1) {
                lastResult = RES_INVALID; publishStatus(); break;
            }
            diagnosticEnabled = payload[0] == 1;
            diagnosticModeChangedPending = true;
            lastResult = RES_OK;
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
           UNO_Q_IP_1 > 0 && UNO_Q_IP_1 < 224 && UNO_Q_IP_1 != 137 &&
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

// ============================================================
// Acceso a registros del GC2145 (banco P0 salvo que se indique otra cosa)
// ============================================================
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

// GC2145 datasheet V1.0: P0:0xb6 bit0 AEC (p.35), P0:0x82 bit1 AWB (p.27).
bool setAutomaticImageControls(bool enabled) {
    return sensorAutoControl(0xb6, 0x01, enabled) &&
           sensorAutoControl(0x82, 0x02, enabled);
}

// Confirma por lectura que AEC y AWB quedaron bloqueados.
bool verifyAutomaticImageControlsLocked() {
    uint8_t aec = 0, awb = 0;
    if (!sensorWrite(0xfe, 0)) return false;
    if (!sensorRead(0xb6, aec) || !sensorRead(0x82, awb)) return false;
    return (aec & 0x01) == 0 && (awb & 0x02) == 0;
}

// Verify, without changing, the full-frame output chosen by cam.begin().
// Crop registers are P0:91..98 in the GC2145 manual.
bool sensorFullOutputVerified() {
    const uint8_t regs[] = {0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99};
    const uint8_t wanted[] = {0,0,0,0,4,176,6,64,0x11};
    if (!sensorWrite(0xfe, 0)) return false;
    for (size_t i=0; i<sizeof(regs); ++i) {
        uint8_t value=0;
        if (!sensorRead(regs[i],value) || value!=wanted[i]) return false;
    }
    return true;
}

// Reinforced tiles use a 384-pixel DCMI crop. Clamping keeps all 320 target
// pixels inside the source for both edge columns.
uint16_t cropOrigin(uint16_t requestedX, bool wide) {
    if (!wide) return requestedX;
    return requestedX > 32 ?
        (requestedX - 32 < FULL_WIDTH-WIDE_WIDTH ? requestedX-32 : FULL_WIDTH-WIDE_WIDTH)
        : 0;
}

bool compactWideTile(uint16_t requestedX, uint16_t sourceX) {
    if (requestedX < sourceX || requestedX + TILE_WIDTH > sourceX + WIDE_WIDTH)
        return false;
    uint8_t *pixels=frameBuffer.getBuffer();
    const size_t sourceStride=size_t(WIDE_WIDTH)*2U;
    const size_t offset=size_t(requestedX-sourceX)*2U;
    for (uint16_t row=0;row<TILE_HEIGHT;++row)
        memmove(pixels+size_t(row)*TILE_WIDTH*2U,
                pixels+size_t(row)*sourceStride+offset,TILE_WIDTH*2U);
    SCB_CleanDCache_by_Addr((uint32_t*)pixels,FRAME_BYTES);
    return true;
}

bool frameBufferValid() {
    return frameBuffer.getBuffer() != nullptr &&
           ((uintptr_t)frameBuffer.getBuffer() & 31U) == 0 &&
           frameBuffer.getBufferSize() >= WIDE_BYTES &&
           cam.frameSize() == (int)(FULL_WIDTH * FULL_HEIGHT * 2U);
}

enum AcquisitionFlag : uint32_t {
    ACQ_CONFIG=1, ACQ_API=2, ACQ_FRAME_EVENT=4, ACQ_DCMI=8,
    ACQ_DMA=16, ACQ_INCOMPLETE=32, ACQ_BUSY=64, ACQ_BUFFER=128
};
volatile bool captureArmed = false;
volatile uint32_t frameEvents=0, dcmiErrors=0, dmaErrors=0;
uint32_t lastCaptureFlags=0, totalFrames=0, totalTileRequests=0;
uint32_t observedFrameEvents=0, observedDmaRemaining=0xffffffffUL, observedDcmiErrors=0, observedDmaErrors=0;
uint32_t observedCaptureResult=0; // 0=success; positive magnitude of driver error

extern "C" void HAL_DCMI_FrameEventCallback(DCMI_HandleTypeDef* handle) {
    if (!captureArmed) return;
    ++frameEvents;
    dcmiErrors |= handle->ErrorCode;
    dmaErrors |= handle->DMA_Handle->ErrorCode;
}
extern "C" void HAL_DCMI_ErrorCallback(DCMI_HandleTypeDef* handle) {
    if (!captureArmed) return;
    dcmiErrors |= handle->ErrorCode;
    dmaErrors |= handle->DMA_Handle->ErrorCode;
}

int captureFrame(uint16_t x, uint16_t y, uint16_t width) {
    ++totalFrames;
    observedFrameEvents=observedDcmiErrors=observedDmaErrors=0; observedDmaRemaining=0xffffffffUL;
    observedCaptureResult=0;
    lastCaptureFlags = 0;
    if (!frameBufferValid()) { lastCaptureFlags=ACQ_BUFFER; observedCaptureResult=1; return -1; }
    if ((DCMI->CR & DCMI_CR_CAPTURE) || (DMA2_Stream3->CR & DMA_SxCR_EN)) {
        lastCaptureFlags=ACQ_BUSY; observedCaptureResult=2; return -1;
    }
    core_util_critical_section_enter();
    frameEvents=dcmiErrors=dmaErrors=0;
    captureArmed = true;
    core_util_critical_section_exit();
    const int result = cam.grabFrameCropped(frameBuffer,x,y,width,TILE_HEIGHT,
                         &frameEvents,FRAME_TIMEOUT_MS,&observedDmaRemaining);
    observedCaptureResult=result < 0 ? uint32_t(-result) : uint32_t(result);
    core_util_critical_section_enter();
    captureArmed = false;
    observedFrameEvents=frameEvents;
    observedDcmiErrors=dcmiErrors; observedDmaErrors=dmaErrors;
    if (result != 0) lastCaptureFlags |= ACQ_API;
    if (frameEvents != 1) lastCaptureFlags |= ACQ_FRAME_EVENT;
    if (dcmiErrors || (DCMI->RISR & (DCMI_FLAG_OVRRI | DCMI_FLAG_ERRRI))) lastCaptureFlags |= ACQ_DCMI;
    if (dmaErrors) lastCaptureFlags |= ACQ_DMA;
    if (observedDmaRemaining != 0) lastCaptureFlags |= ACQ_INCOMPLETE;
    if ((DCMI->CR & DCMI_CR_CAPTURE) || (DMA2_Stream3->CR & DMA_SxCR_EN)) lastCaptureFlags |= ACQ_BUSY;
    core_util_critical_section_exit();
    return lastCaptureFlags ? -1 : 0;
}

int captureAfterDiscard(uint8_t discardFrames, uint16_t x, uint16_t y, uint16_t width) {
    for (uint8_t discarded = 0; discarded < discardFrames; ++discarded) {
        if (captureFrame(x,y,width) != 0) return -1;
    }
    return captureFrame(x,y,width);
}

uint8_t prepareCamera() {
    if (!validNetwork()) return ST_NETWORK_CONFIG_ERROR;
    if (!cameraReady) {
        Serial.println("NICLA | CAMERA INITIALIZING");
        cam.debug(Serial);
        cameraReady = cam.begin(CAMERA_R1600x1200, CAMERA_RGB565, 30);
        if (!cameraReady) return ST_CAMERA_INIT_ERROR;
        Serial.println("NICLA | CAMERA INITIALIZED");
    }
    if (cam.setVerticalFlip(true) != 0 || cam.setHorizontalMirror(true) != 0)
        return ST_CAMERA_INIT_ERROR;
    Serial.println("NICLA | WIFI CONNECTING");
    if (!connectWiFi()) return ST_WIFI_ERROR;
    Serial.println("NICLA | FULL SENSOR OUTPUT VERIFY");
    if (!sensorFullOutputVerified() || !frameBufferValid()) return ST_BUFFER_ERROR;
    if (!setAutomaticImageControls(true)) return ST_EXPOSURE_ERROR;
    Serial.println("NICLA | EXPOSURE WARMUP");
    delay(1500);
    if (captureAfterDiscard(2,0,0,TILE_WIDTH) != 0) return ST_CAPTURE_ERROR;
    if (!setAutomaticImageControls(false)) return ST_EXPOSURE_ERROR;
    if (!verifyAutomaticImageControlsLocked()) return ST_EXPOSURE_ERROR;
    return ST_READY;
}

bool sendAll(TCPSocket& socket, const uint8_t* data, size_t length) {
    size_t offset = 0;
    while (offset < length) {
        const nsapi_size_or_error_t sent = socket.send(data + offset, length - offset > 1024 ? 1024 : length - offset);
        if (sent <= 0) return false;
        offset += (size_t)sent;
    }
    return true;
}

// ============================================================
// Envío de un tile — encabezado único, sin tráiler
// ============================================================
// CRC_A is always present, including diagnostics OFF. No CRC_TX or trailer.
uint8_t sendTileOverTcp(const SendTileRequest& request, uint32_t capturedMs,
                       uint32_t captureStartedMs, uint32_t discardMs, uint32_t frameMs,
                       bool wideWindow, uint32_t flags) {
    if (WiFi.status() != WL_CONNECTED) return ST_WIFI_ERROR;
    const bool diagnostics = diagnosticEnabled;
    const uint32_t crcA = crc32_ieee(frameBuffer.getBuffer(), FRAME_BYTES);
    char address[16];
    snprintf(address, sizeof(address), "%d.%d.%d.%d", UNO_Q_IP_1, UNO_Q_IP_2, UNO_Q_IP_3, UNO_Q_IP_4);
    SocketAddress destination;
    if (!destination.set_ip_address(address)) return ST_NETWORK_CONFIG_ERROR;
    destination.set_port(UNO_Q_PORT);
    TCPSocket socket;
    if (socket.open(WiFi.getNetwork()) != NSAPI_ERROR_OK) return ST_TCP_CONNECT_ERROR;
    socket.set_timeout(TCP_TIMEOUT_MS);
    if (socket.connect(destination) != NSAPI_ERROR_OK) {
        socket.close(); return ST_TCP_CONNECT_ERROR;
    }
    char header[512];
    const int length = snprintf(header, sizeof(header),
        "SIXQ_TILE_V7\nRGB565\n"
        "%u\n%u\n%lu\n%lu\n%u\n%u\n%u\n%u\n%u\n%u\n%u\n"
        "%lu\n%lu\n%lu\n%lu\n%u\n%lu\n%u\n%lu\n%lu\n%lu\n%lu\n%lu\n%lu\n\n",
        TILE_WIDTH, TILE_HEIGHT, (unsigned long)FRAME_BYTES,
        (unsigned long)request.captureId, request.captureSeq, request.tileId,
        request.x, request.y, request.requestOrdinal, request.reinforced ? 1U : 0U, request.attempt,
        (unsigned long)capturedMs, (unsigned long)(capturedMs-captureStartedMs),
        (unsigned long)discardMs, (unsigned long)frameMs, diagnostics ? 1U : 0U,
        (unsigned long)crcA, wideWindow ? 1U : 0U,
        (unsigned long)flags, (unsigned long)observedCaptureResult,
        (unsigned long)observedFrameEvents,
        (unsigned long)observedDmaRemaining, (unsigned long)observedDcmiErrors,
        (unsigned long)observedDmaErrors);
    const bool sent = length>0 && length<(int)sizeof(header) &&
        sendAll(socket,(const uint8_t*)header,length) && sendAll(socket,frameBuffer.getBuffer(),FRAME_BYTES);
    socket.close();
    if (diagnostics) {
        Serial.print("NICLA | V7 | TILE="); Serial.print(request.tileId);
        Serial.print(" | ATTEMPT="); Serial.print(request.attempt);
        Serial.print(" | ACQUISITION_FLAGS="); Serial.print(flags);
        Serial.print(" | WIDE_WINDOW="); Serial.print(wideWindow);
        Serial.print(" | SEND_OK="); Serial.println(sent);
    }
    return sent ? SEND_OK : ST_TCP_WRITE_ERROR;
}

void runSendTile(const SendTileRequest& request) {
    niclaState=ST_CAPTURE; lastResult=RES_OK; publishStatus();
    ++totalTileRequests;
    const bool wideWindow=request.reinforced;
    const uint16_t width=wideWindow ? WIDE_WIDTH : TILE_WIDTH;
    const uint16_t origin=cropOrigin(request.x,wideWindow);
    if (!frameBufferValid()) {
        niclaState=ST_BUFFER_ERROR; busy=false; publishStatus(); return;
    }
    // If setup fails before DMA starts, archived invalid data must not be a
    // stale tile from an earlier request or uninitialized heap contents.
    memset(frameBuffer.getBuffer(),0,WIDE_BYTES);
    uint32_t flags=0;
    const uint32_t captureStartedMs=millis();
    const uint8_t discards=request.reinforced ? DISCARD_FRAMES_REINFORCED : DISCARD_FRAMES_NORMAL;
    for (uint8_t i=0;i<discards;++i) {
        if (captureFrame(origin,request.y,width)!=0) { flags|=lastCaptureFlags; break; }
    }
    const uint32_t finalFrameStartedMs=millis();
    if (!flags && captureFrame(origin,request.y,width)!=0) flags|=lastCaptureFlags;
    // Forward row order cannot overwrite a later source row.
    if (wideWindow && !compactWideTile(request.x,origin)) {
        niclaState=ST_BUFFER_ERROR; busy=false; publishStatus(); return;
    }
    const uint32_t capturedMs=millis();
    if (!frameBufferValid() || (DCMI->CR & DCMI_CR_CAPTURE) || (DMA2_Stream3->CR & DMA_SxCR_EN)) {
        niclaState=ST_CAPTURE_ERROR; busy=false; publishStatus(); return;
    }
    // A stopped but invalid acquisition can be archived, never accepted as good.
    niclaState=ST_SEND; publishStatus();
    const uint8_t result=sendTileOverTcp(request,capturedMs,captureStartedMs,
        finalFrameStartedMs-captureStartedMs,capturedMs-finalFrameStartedMs,
        wideWindow,flags);
    niclaState=result==SEND_OK ? ST_READY : result;
    lastResult=RES_OK; busy=false; publishStatus();
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
    pinMode(PB_8, INPUT_PULLUP);
    pinMode(PB_9, INPUT_PULLUP);

    Wire.begin(0x42);
    Wire.onReceive(onReceive);
    Wire.onRequest(onRequest);
    niclaState = ST_BOOT;
    publishStatus();

    const uint32_t serialWaitStartedMs = millis();
    while (!Serial && millis() - serialWaitStartedMs < SERIAL_ATTACH_WAIT_MS) {
        delay(10);
    }
    Serial.println("NICLA | SIX_Q V7 WIRE9 | FIXED SENSOR + DCMI CROP | WAITING FOR AUTH_CHALLENGE");
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
    } else if (diagnosticModeChangedPending) {
        diagnosticModeChangedPending = false;
        Serial.print("NICLA | DIAGNOSTIC_MODE=");
        Serial.println(diagnosticEnabled ? "ON" : "OFF");
    }
    printHeartbeat();
    delay(5);
}
