// SPDX-FileCopyrightText: 2026 Alberto Prestes
// SPDX-License-Identifier: 0BSD

#include <Wire.h>
#include <Arduino_RouterBridge.h>
#include <SparkFun_Qwiic_Humidity_AHT20.h>
#include <string.h>


/*
 * SIX_Q - UNO Q MCU /////// VERSION 7 ///////
 *
 * Current jobs:
 *
 * - Read AHT20 temperature and humidity.
 * - Read the physical WATER push control.
 * - Read the physical LIGHT persistent switch.
 * - Apply WATER and LIGHT rules.
 * - Drive relay R2 for WATER.
 * - Drive relay R3 for LIGHT.
 * - Drive the green status LED on OUT5.
 * - Provide the Nicla Vision V7 I2C bridge with explicit capture profiles and attempts.
 *
 *
 * CONTROL PRINCIPLE
 *
 * Physical controls and web controls have the same value.
 *
 * Neither one controls a relay directly.
 *
 * Both generate a request.
 * The request goes through the same MCU rules.
 * Only then is the relay allowed to change.
 *
 *
 * WATER RULE
 *
 * One accepted WATER request = one complete 5 second cycle.
 *
 * The cycle cannot be extended or restarted while active.
 *
 * Maximum:
 *
 *     4 cycles x 5 seconds = 20 seconds
 *
 * per 24 hour prototype window.
 *
 * The 24 hour window is currently based on MCU uptime.
 * It is NOT yet a calendar-day implementation.
 */


// ============================================================
// Relay hardware
// ============================================================

// Arduino 4 Relays Shield.
//
// Relay R1 is intentionally unused.
//
// During prototype testing an intermittent behavior was observed
// on that channel. There is no reason to build an important
// function on top of a channel we do not fully trust yet.
//
// R4 is also left unused for future expansion.

constexpr pin_size_t RELAY_R1 =
    4;

constexpr pin_size_t RELAY_R2_WATER =
    7;

constexpr pin_size_t RELAY_R3_LIGHT =
    8;

constexpr pin_size_t RELAY_R4 =
    12;


// ============================================================
// Local controls
// ============================================================

// IN2 = momentary WATER push control.
//
// Press:
//     request one complete 5 second WATER cycle.
//
// Release:
//     no action.

constexpr pin_size_t IN2_WATER_PUSH =
    A2;


// IN3 = persistent LIGHT switch.
//
// Its stable physical position becomes the requested
// LIGHT state.

constexpr pin_size_t IN3_LIGHT_SWITCH =
    A3;


// INPUT_PULLUP:
//
// inactive = HIGH
// active   = LOW

constexpr int INPUT_ACTIVE_LEVEL =
    LOW;


// ============================================================
// Auxiliary output
// ============================================================

// OUT5 carries the green status LED.
//
// LOW  = MCU still starting.
// HIGH = MCU reached normal runtime.

constexpr pin_size_t OUT5_GREEN_LED =
    5;


// ============================================================
// Timing
// ============================================================

constexpr unsigned long SENSOR_SAMPLE_MS =
    2000UL;

constexpr unsigned long STARTUP_REPORT_MS =
    5000UL;

constexpr unsigned long DEBOUNCE_MS =
    40UL;


// WATER is handled as discrete irrigation units.

constexpr unsigned long WATER_CYCLE_MS =
    5000UL;

constexpr uint8_t MAX_WATER_CYCLES_PER_WINDOW =
    4;


// Program state is owned by the MCU and resets on MCU restart.
int waterProgramHours = 0;
int lightProgramHours = 0;
int clockDay = -1; // Python local date ordinal.
int waterQuotaDay = -1;
int64_t waterScheduleCursor = -1;
void updateProgramClock();
int clockSeconds = -1;
unsigned long clockTickAt = 0;

// ============================================================
// AHT20
// ============================================================

AHT20 aht;

bool sensorOK =
    false;

bool startupReportSent =
    false;

unsigned long lastSensorSample =
    0;


// ============================================================
// Authoritative relay state
// ============================================================

volatile bool waterRelayOn =
    false;

volatile bool lightRelayOn =
    false;


// ============================================================
// WATER cycle state
// ============================================================

bool waterCycleActive =
    false;

unsigned long waterCycleStartedAt =
    0;

uint8_t waterCyclesUsed =
    0;



// ============================================================
// Debounced physical input
// ============================================================

struct DebouncedInput {

    pin_size_t pin;

    int rawState;

    int stableState;

    unsigned long lastChange;
};


DebouncedInput waterInput = {

    IN2_WATER_PUSH,

    HIGH,

    HIGH,

    0
};


DebouncedInput lightInput = {

    IN3_LIGHT_SWITCH,

    HIGH,

    HIGH,

    0
};


// ============================================================
// Nicla Vision V7 protocol
// ============================================================

// The 23-byte I2C response frame remains; all V7 peers require wire revision 9.
//
// Do not casually change them during integration.

constexpr uint8_t NICLA_ADDRESS =
    0x42;

constexpr uint8_t FRAME_SOF =
    0xA5;

constexpr uint8_t PROTOCOL_VERSION =
    9; // V7 wire revision


constexpr uint8_t CMD_AUTH_CHALLENGE =
    0x01;

constexpr uint8_t CMD_AUTH_RESPONSE =
    0x02;

constexpr uint8_t CMD_AUTH_ACK =
    0x03;

constexpr uint8_t CMD_SEND_TILE =
    0x11;

constexpr uint8_t CMD_SET_DIAGNOSTIC =
    0x13;

constexpr uint8_t CMD_STATUS =
    0xF0;


constexpr uint8_t I2C_RETRIES =
    4;

constexpr uint8_t MAX_OUTGOING_PAYLOAD =
    16;

constexpr uint8_t RESPONSE_FRAME_SIZE =
    23;


uint8_t nextSeq =
    0;

uint8_t lastFrame[
    RESPONSE_FRAME_SIZE
] = {};

bool lastFrameValid =
    false;

uint8_t lastReadBytes =
    0;


// ============================================================
// Digital helper
// ============================================================

int readDigitalPin(
    const pin_size_t pin
) {

    return
        digitalRead(pin) == HIGH
        ? 1
        : 0;
}


// ============================================================
// WATER rule
// ============================================================

bool waterRequestAllowed(
    bool desiredOn
) {

    /*
     * WATER OFF is currently an acknowledgement/no-op.
     *
     * Once an irrigation cycle has been accepted,
     * it owns its complete 5 second interval.
     *
     * A future emergency-stop command should be implemented
     * separately from the normal WATER request.
     */

    if (!desiredOn) {

        return true;
    }


    // RULE:
    // Never restart or extend an active irrigation cycle.

    if (waterCycleActive) {

        return false;
    }


    // RULE:
    // Maximum four complete irrigation cycles per window.

    if (
        waterCyclesUsed >=
        MAX_WATER_CYCLES_PER_WINDOW
    ) {

        return false;
    }


    return true;
}


// ============================================================
// LIGHT rule
// ============================================================

bool lightRequestAllowed(
    bool desiredOn
) {

    /*
     * Manual lighting has no quota; updatePrograms evaluates scheduled windows.
     *
     * The physical switch does not own the relay.
     * The web page does not own the relay.
     *
     * This function owns permission.
     */

    (void)desiredOn;

    return true;
}


// ============================================================
// Apply WATER request
// ============================================================

bool applyWaterRequest(
    bool desiredOn
) {
    updateProgramClock();

    if (
        !waterRequestAllowed(
            desiredOn
        )
    ) {

        return false;
    }


    /*
     * OFF does not interrupt an accepted irrigation unit.
     *
     * This preserves the rule:
     *
     *     one irrigation = exactly 5 seconds.
     */

    if (!desiredOn) {

        return true;
    }


    /*
     * Start exactly one irrigation unit.
     *
     * Any later ON request while this is active will be rejected.
     */

    digitalWrite(
        RELAY_R2_WATER,
        HIGH
    );


    waterCyclesUsed++;

    waterRelayOn =
        true;

    waterCycleActive =
        true;

    waterCycleStartedAt =
        millis();


    return true;
}


// ============================================================
// Apply LIGHT request
// ============================================================

bool applyLightRequest(
    bool desiredOn
) {

    if (
        !lightRequestAllowed(
            desiredOn
        )
    ) {

        return false;
    }


    digitalWrite(
        RELAY_R3_LIGHT,

        desiredOn
        ? HIGH
        : LOW
    );


    lightRelayOn =
        desiredOn;


    return true;
}


// ============================================================
// Report authoritative relay state
// ============================================================

void reportControlState() {

    Bridge.notify(
        "control_state",

        waterRelayOn
        ? 1
        : 0,

        lightRelayOn
        ? 1
        : 0
    );
}


// ============================================================
// WATER daily prototype window
// ============================================================

void updateProgramClock() {
    if (clockSeconds < 0 || clockDay < 0) return;
    const unsigned long now = millis();
    const unsigned long elapsed = (now - clockTickAt) / 1000UL;
    const unsigned long seconds = clockSeconds + elapsed;
    clockDay += seconds / 86400UL;
    clockSeconds = seconds % 86400UL;
    clockTickAt += elapsed * 1000UL;
    if (waterQuotaDay < 0) {
        waterQuotaDay = clockDay;
    } else if (clockDay > waterQuotaDay) {
        // A pulse spanning midnight belongs to the day it was accepted.
        waterCyclesUsed = 0;
        waterQuotaDay = clockDay;
    }
    // Backwards clock corrections cannot grant another daily quota.
}


// ============================================================
// WATER cycle timeout
// ============================================================

void updateWaterCycle(
    const unsigned long now
) {

    if (!waterCycleActive) {

        return;
    }


    if (
        now -
        waterCycleStartedAt
        <
        WATER_CYCLE_MS
    ) {

        return;
    }


    /*
     * One full irrigation unit has finished.
     */

    digitalWrite(
        RELAY_R2_WATER,
        LOW
    );


    waterRelayOn =
        false;

    waterCycleActive =
        false;


    /*
     * The MCU is authoritative.
     *
     * Tell Linux/WebUI that WATER is now physically OFF.
     */

    reportControlState();
}


// ============================================================
// Report physical control event
// ============================================================

void reportPhysicalControlEvent(
    int subsystem,
    bool requestedState,
    bool accepted,
    bool actualState
) {

    /*
     * subsystem:
     *
     * 1 = WATER
     * 2 = LIGHT
     *
     * The source is already known to be PHYSICAL because
     * this notification only comes from the physical input task.
     */

    Bridge.notify(
        "physical_control_event",

        subsystem,

        requestedState
        ? 1
        : 0,

        accepted
        ? 1
        : 0,

        actualState
        ? 1
        : 0
    );
}


// Linux supplies the local date and time; MCU owns scheduling and quotas.
int rpcSetClock(int day, int seconds) {
    if (day <= 0 || seconds < 0 || seconds >= 86400) return 0;
    clockDay = day;
    clockSeconds = seconds;
    clockTickAt = millis();
    updateProgramClock();
    return 1;
}

int rpcSetWaterProgram(int hours) {
    if (hours != 0 && hours != 4 && hours != 6 && hours != 8 && hours != 12 && hours != 24) return 0;
    if (hours != 0 && clockDay < 0) return 0;
    updateProgramClock();
    waterProgramHours = hours;
    const int64_t current = int64_t(clockDay) * 86400 + clockSeconds;
    // Selection waits for the next slot; re-selection never replays a slot.
    if (current > waterScheduleCursor) waterScheduleCursor = current;
    return 1;
}

int rpcSetLightProgram(int hours) {
    if (hours != 0 && hours != 6 && hours != 8 && hours != 12 && hours != 18) return 0;
    if (hours != 0 && clockSeconds < 0) return 0;
    lightProgramHours = hours;
    return 1;
}

int rpcGetWaterProgram() { return waterProgramHours; }
int rpcGetLightProgram() { return lightProgramHours; }
int rpcGetWaterUsed() { return waterCyclesUsed; }

void updatePrograms() {
    // Safe RPCs and program evaluation share the loop thread.
    updateProgramClock();
    if (clockDay < 0 || clockSeconds < 0) return;
    const int64_t current = int64_t(clockDay) * 86400 + clockSeconds;
    if (current > waterScheduleCursor) {
        const int hour = clockSeconds / 3600;
        const int64_t slot = int64_t(clockDay) * 86400 + hour * 3600;
        const bool scheduled = waterProgramHours == 24 ? hour == 12 :
            (waterProgramHours > 0 && hour % waterProgramHours == 0);
        // Allow normal loop/sync latency within the scheduled minute only.
        // No catch-up of old slots, duplicate slots, or denied requests.
        const bool due = scheduled && slot > waterScheduleCursor && clockSeconds % 3600 < 60;
        waterScheduleCursor = current;
        if (due && applyWaterRequest(true)) reportControlState();
    }
    if (lightProgramHours > 0 && clockSeconds >= 0) {
        const bool desired = clockSeconds >= 6 * 3600 &&
            clockSeconds < (6 + lightProgramHours) * 3600;
        if (desired != lightRelayOn) {
            applyLightRequest(desired);
            reportControlState();
        }
    }
}

// ============================================================
// Linux -> MCU WATER request
// ============================================================

int rpcRequestWater(
    int requestedState
) {
    if (!requestedState) waterProgramHours = 0;

    const bool desiredOn =
        requestedState != 0;


    /*
     * ON:
     *     request one complete 5 second irrigation unit.
     *
     * OFF:
     *     acknowledged but does not interrupt an active unit.
     *
     * Physical and web requests use the same rule.
     */

    return
        applyWaterRequest(
            desiredOn
        )
        ? 1
        : 0;
}


// ============================================================
// Linux -> MCU LIGHT request
// ============================================================

int rpcRequestLight(
    int requestedState
) {
    lightProgramHours = 0;

    const bool desiredOn =
        requestedState != 0;


    return
        applyLightRequest(
            desiredOn
        )
        ? 1
        : 0;
}


// ============================================================
// Linux -> MCU state requests
// ============================================================

int rpcGetWaterState() {

    return
        waterRelayOn
        ? 1
        : 0;
}


int rpcGetLightState() {

    return
        lightRelayOn
        ? 1
        : 0;
}


// ============================================================
// AHT20 sampling
// ============================================================

void updateEnvironment(
    const unsigned long now
) {

    if (!sensorOK) {

        return;
    }


    if (
        now -
        lastSensorSample
        <
        SENSOR_SAMPLE_MS
    ) {

        return;
    }


    lastSensorSample =
        now;


    const float temperature =
        aht.getTemperature();

    const float humidity =
        aht.getHumidity();


    Bridge.notify(
        "environment_sample",

        temperature,

        humidity
    );
}


// ============================================================
// Debounce helper
// ============================================================

bool updateDebouncedInput(
    DebouncedInput& input,
    const unsigned long now,
    int& newStableState
) {

    const int raw =
        readDigitalPin(
            input.pin
        );


    if (
        raw !=
        input.rawState
    ) {

        input.rawState =
            raw;

        input.lastChange =
            now;
    }


    if (
        now -
        input.lastChange
        <
        DEBOUNCE_MS
    ) {

        return false;
    }


    if (
        raw ==
        input.stableState
    ) {

        return false;
    }


    input.stableState =
        raw;

    newStableState =
        raw;


    return true;
}


// ============================================================
// Physical controls
// ============================================================

void updatePhysicalControls(
    const unsigned long now
) {

    int newState =
        HIGH;


    // --------------------------------------------------------
    // WATER / IN2
    // --------------------------------------------------------

    if (
        updateDebouncedInput(
            waterInput,
            now,
            newState
        )
    ) {

        /*
         * IN2 is a trigger, not a maintained command.
         *
         * Only the PRESS edge matters.
         *
         * Press:
         *     request one complete 5 second irrigation unit.
         *
         * Release:
         *     intentionally ignored.
         */

        if (
            newState ==
            INPUT_ACTIVE_LEVEL
        ) {

            const bool accepted =
                applyWaterRequest(
                    true
                );


            reportPhysicalControlEvent(
                1,
                true,
                accepted,
                waterRelayOn
            );


            reportControlState();
        }
    }


    // --------------------------------------------------------
    // LIGHT / IN3
    // --------------------------------------------------------

    if (
        updateDebouncedInput(
            lightInput,
            now,
            newState
        )
    ) {

        /*
         * IN3 is persistent.
         *
         * Its stable electrical state becomes the requested
         * LIGHT state.
         */

        lightProgramHours = 0;
        const bool desiredOn =
            newState ==
            INPUT_ACTIVE_LEVEL;


        const bool accepted =
            applyLightRequest(
                desiredOn
            );


        reportPhysicalControlEvent(
            2,
            desiredOn,
            accepted,
            lightRelayOn
        );


        reportControlState();
    }
}


// ============================================================
// Startup report
// ============================================================

void updateStartupReport(
    const unsigned long now
) {

    if (
        startupReportSent
    ) {

        return;
    }


    if (
        now <
        STARTUP_REPORT_MS
    ) {

        return;
    }


    Bridge.notify(
        "sensor_status",

        sensorOK
        ? 1
        : 0
    );


    reportControlState();


    startupReportSent =
        true;
}


// ============================================================
// Vision V7 - CRC16
// ============================================================

uint16_t crc16_ccitt(
    const uint8_t* data,
    size_t len
) {

    uint16_t crc =
        0xFFFF;


    for (
        size_t i = 0;
        i < len;
        ++i
    ) {

        crc ^=
            (uint16_t)data[i]
            << 8;


        for (
            int bit = 0;
            bit < 8;
            ++bit
        ) {

            crc =
                (
                    crc &
                    0x8000
                )
                ?
                (uint16_t)(
                    (crc << 1) ^
                    0x1021
                )
                :
                (uint16_t)(
                    crc << 1
                );
        }
    }


    return crc;
}


// ============================================================
// Vision V7 - send frame
// ============================================================

bool sendFrame(
    uint8_t cmd,
    const uint8_t* payload,
    uint8_t payloadLen
) {

    uint8_t frame[
        5 +
        MAX_OUTGOING_PAYLOAD +
        2
    ];


    frame[0] =
        FRAME_SOF;

    frame[1] =
        PROTOCOL_VERSION;

    frame[2] =
        cmd;

    frame[3] =
        nextSeq;

    frame[4] =
        payloadLen;


    if (
        payload &&
        payloadLen
    ) {

        memcpy(
            frame + 5,
            payload,
            payloadLen
        );
    }


    const uint16_t crc =
        crc16_ccitt(
            frame,
            5 + payloadLen
        );


    frame[
        5 +
        payloadLen
    ] =
        (uint8_t)(
            crc >> 8
        );


    frame[
        6 +
        payloadLen
    ] =
        (uint8_t)(
            crc &
            0xFF
        );


    const size_t total =
        7 +
        payloadLen;


    for (
        uint8_t attempt = 0;
        attempt < I2C_RETRIES;
        ++attempt
    ) {

        Wire.beginTransmission(
            NICLA_ADDRESS
        );


        Wire.write(
            frame,
            total
        );


        if (
            Wire.endTransmission()
            ==
            0
        ) {

            nextSeq++;

            return true;
        }


        delay(25);
    }


    return false;
}


// ============================================================
// Vision V7 - read response
// ============================================================

bool readResponseFrame(
    uint8_t* outFrame
) {

    for (
        uint8_t attempt = 0;
        attempt < I2C_RETRIES;
        ++attempt
    ) {

        const uint8_t received =
            Wire.requestFrom(
                NICLA_ADDRESS,
                (uint8_t)RESPONSE_FRAME_SIZE
            );


        lastReadBytes =
            received;


        if (
            received ==
            RESPONSE_FRAME_SIZE
        ) {

            for (
                uint8_t i = 0;
                i < RESPONSE_FRAME_SIZE;
                ++i
            ) {

                outFrame[i] =
                    (uint8_t)Wire.read();
            }


            return true;
        }


        while (
            Wire.available()
        ) {

            Wire.read();
        }


        delay(25);
    }


    return false;
}


// ============================================================
// Vision V7 - parse frame
// ============================================================

bool parseFrame(
    const uint8_t* frame,
    uint8_t& cmd,
    const uint8_t*& payload,
    uint8_t& payloadLen
) {

    if (
        frame[0] != FRAME_SOF
        ||
        frame[1] != PROTOCOL_VERSION
    ) {

        return false;
    }


    cmd =
        frame[2];

    payloadLen =
        frame[4];


    if (
        payloadLen >
        RESPONSE_FRAME_SIZE - 7
    ) {

        return false;
    }


    const uint16_t receivedCrc =

        (
            (uint16_t)
            frame[
                5 +
                payloadLen
            ]
            << 8
        )

        |

        frame[
            6 +
            payloadLen
        ];


    if (
        crc16_ccitt(
            frame,
            5 + payloadLen
        )
        !=
        receivedCrc
    ) {

        return false;
    }


    payload =
        frame + 5;


    return true;
}


// ============================================================
// Vision RPC - poll response
// ============================================================

bool visionPollResponse() {

    lastFrameValid =
        readResponseFrame(
            lastFrame
        );


    if (
        !lastFrameValid
    ) {

        return false;
    }


    uint8_t cmd;

    uint8_t payloadLen;

    const uint8_t* payload;


    lastFrameValid =
        parseFrame(
            lastFrame,
            cmd,
            payload,
            payloadLen
        );


    return lastFrameValid;
}


// ============================================================
// Vision RPC - response command
// ============================================================

int visionResponseCommand() {

    uint8_t cmd;

    uint8_t payloadLen;

    const uint8_t* payload;


    if (
        !lastFrameValid
        ||
        !parseFrame(
            lastFrame,
            cmd,
            payload,
            payloadLen
        )
    ) {

        return -1;
    }


    return cmd;
}


// ============================================================
// Vision RPC - state
// ============================================================

int visionStatus() {

    uint8_t cmd;

    uint8_t payloadLen;

    const uint8_t* payload;


    if (
        !lastFrameValid
        ||
        !parseFrame(
            lastFrame,
            cmd,
            payload,
            payloadLen
        )
    ) {

        return 255;
    }


    if (
        cmd ==
        CMD_STATUS
        &&
        payloadLen >= 1
    ) {

        return payload[0];
    }


    return 253;
}


// ============================================================
// Vision RPC - last result
// ============================================================

int visionLastResult() {

    uint8_t cmd;

    uint8_t payloadLen;

    const uint8_t* payload;


    if (
        !lastFrameValid
        ||
        !parseFrame(
            lastFrame,
            cmd,
            payload,
            payloadLen
        )
    ) {

        return -1;
    }


    if (
        cmd ==
        CMD_STATUS
        &&
        payloadLen >= 2
    ) {

        return payload[1];
    }


    return -1;
}


// ============================================================
// Vision RPC - confirmed diagnostic state
// ============================================================

int visionDiagnosticEnabled() {

    uint8_t cmd;

    uint8_t payloadLen;

    const uint8_t* payload;


    if (
        !lastFrameValid
        ||
        !parseFrame(
            lastFrame,
            cmd,
            payload,
            payloadLen
        )
    ) {

        return -1;
    }


    if (
        cmd == CMD_STATUS
        &&
        payloadLen >= 4
    ) {

        return payload[3] ? 1 : 0;
    }


    return -1;
}


// ============================================================
// Vision RPC - HMAC word
// ============================================================

uint32_t visionAuthMacWord(
    int index
) {

    uint8_t cmd;

    uint8_t payloadLen;

    const uint8_t* payload;


    if (
        !lastFrameValid
        ||
        !parseFrame(
            lastFrame,
            cmd,
            payload,
            payloadLen
        )
    ) {

        return 0;
    }


    if (
        cmd != CMD_AUTH_RESPONSE
        ||
        payloadLen < 16
        ||
        index < 0
        ||
        index > 3
    ) {

        return 0;
    }


    const uint8_t* p =
        payload +
        index * 4;


    return

        ((uint32_t)p[0] << 24)

        |

        ((uint32_t)p[1] << 16)

        |

        ((uint32_t)p[2] << 8)

        |

        p[3];
}


// ============================================================
// Vision RPC - AUTH challenge
// ============================================================

bool visionAuthChallenge(
    uint32_t nonceHi,
    uint32_t nonceLo
) {

    uint8_t payload[8] = {

        (uint8_t)(
            nonceHi >> 24
        ),

        (uint8_t)(
            nonceHi >> 16
        ),

        (uint8_t)(
            nonceHi >> 8
        ),

        (uint8_t)nonceHi,


        (uint8_t)(
            nonceLo >> 24
        ),

        (uint8_t)(
            nonceLo >> 16
        ),

        (uint8_t)(
            nonceLo >> 8
        ),

        (uint8_t)nonceLo
    };


    return sendFrame(
        CMD_AUTH_CHALLENGE,
        payload,
        sizeof(payload)
    );
}


// ============================================================
// Vision RPC - AUTH ACK
// ============================================================

bool visionAuthAck(
    bool ok
) {

    uint8_t payload[1] = {

        (uint8_t)(
            ok
            ? 1
            : 0
        )
    };


    return sendFrame(
        CMD_AUTH_ACK,
        payload,
        sizeof(payload)
    );
}


// ============================================================
// Vision RPC - SEND TILE
// ============================================================

bool visionSendTile(uint32_t captureId, int captureSeq, int tileId,
                    int x, int y, int requestOrdinal, int reinforced, int attempt) {
    if (!captureId || captureSeq < 0 || captureSeq > 255 || tileId < 0 || tileId >= 20 ||
        requestOrdinal < 0 || requestOrdinal >= 20 || attempt < 0 || attempt > 5 ||
        reinforced < 0 || reinforced > 1 || x != (tileId % 5) * 320 ||
        y != (tileId / 5 == 3 ? 880 : (tileId / 5) * 320)) return false;
    const uint8_t payload[13] = {
        uint8_t(captureId >> 24), uint8_t(captureId >> 16), uint8_t(captureId >> 8), uint8_t(captureId),
        uint8_t(captureSeq), uint8_t(tileId), uint8_t(x >> 8), uint8_t(x),
        uint8_t(y >> 8), uint8_t(y), uint8_t(requestOrdinal), uint8_t(reinforced), uint8_t(attempt)
    };
    return sendFrame(CMD_SEND_TILE, payload, sizeof(payload));
}

// ============================================================
// Vision RPC - enable/disable detailed V7 diagnostics
// ============================================================

bool visionSetDiagnostic(
    bool enabled
) {

    const uint8_t payload[1] = {

        enabled
        ? (uint8_t)1
        : (uint8_t)0
    };


    return sendFrame(
        CMD_SET_DIAGNOSTIC,
        payload,
        sizeof(payload)
    );
}


// ============================================================
// Setup
// ============================================================

void setup() {

    Bridge.begin();


    // --------------------------------------------------------
    // Relays
    // --------------------------------------------------------

    pinMode(
        RELAY_R1,
        OUTPUT
    );

    pinMode(
        RELAY_R2_WATER,
        OUTPUT
    );

    pinMode(
        RELAY_R3_LIGHT,
        OUTPUT
    );

    pinMode(
        RELAY_R4,
        OUTPUT
    );


    // Safe startup:
    //
    // everything begins OFF.

    digitalWrite(
        RELAY_R1,
        LOW
    );

    digitalWrite(
        RELAY_R2_WATER,
        LOW
    );

    digitalWrite(
        RELAY_R3_LIGHT,
        LOW
    );

    digitalWrite(
        RELAY_R4,
        LOW
    );


    waterRelayOn =
        false;

    lightRelayOn =
        false;

    waterCycleActive =
        false;

    waterCyclesUsed =
        0;



    // --------------------------------------------------------
    // Green LED
    // --------------------------------------------------------

    pinMode(
        OUT5_GREEN_LED,
        OUTPUT
    );


    digitalWrite(
        OUT5_GREEN_LED,
        LOW
    );


    // --------------------------------------------------------
    // Physical inputs
    // --------------------------------------------------------

    pinMode(
        IN2_WATER_PUSH,
        INPUT_PULLUP
    );


    pinMode(
        IN3_LIGHT_SWITCH,
        INPUT_PULLUP
    );


    waterInput.rawState =
        readDigitalPin(
            IN2_WATER_PUSH
        );


    waterInput.stableState =
        waterInput.rawState;


    lightInput.rawState =
        readDigitalPin(
            IN3_LIGHT_SWITCH
        );


    lightInput.stableState =
        lightInput.rawState;


    /*
     * LIGHT is persistent, so its startup position matters.
     *
     * WATER is a trigger.
     *
     * We deliberately do NOT start irrigation simply because
     * the push button happens to be held during boot.
     */

    applyLightRequest(
        lightInput.stableState ==
        INPUT_ACTIVE_LEVEL
    );


    // --------------------------------------------------------
    // AHT20 / Wire1
    // --------------------------------------------------------

    Wire1.begin();


    sensorOK =
        aht.begin(
            Wire1
        );


    // --------------------------------------------------------
    // Nicla control / Wire
    // --------------------------------------------------------

    Wire.begin();


    Wire.setClock(
        100000
    );


    // --------------------------------------------------------
    // Control RPC
    // --------------------------------------------------------

    Bridge.provide_safe("set_program_clock", rpcSetClock);
    Bridge.provide_safe("set_water_program", rpcSetWaterProgram);
    Bridge.provide_safe("set_light_program", rpcSetLightProgram);
    Bridge.provide_safe("get_water_program", rpcGetWaterProgram);
    Bridge.provide_safe("get_light_program", rpcGetLightProgram);
    Bridge.provide_safe("get_water_used", rpcGetWaterUsed);

    Bridge.provide_safe(
        "request_water",
        rpcRequestWater
    );


    Bridge.provide_safe(
        "request_light",
        rpcRequestLight
    );


    Bridge.provide_safe(
        "get_water_state",
        rpcGetWaterState
    );


    Bridge.provide_safe(
        "get_light_state",
        rpcGetLightState
    );


    // --------------------------------------------------------
    // Vision V7 RPC
    // --------------------------------------------------------

    Bridge.provide(
        "vision_poll_response",
        visionPollResponse
    );


    Bridge.provide(
        "vision_response_command",
        visionResponseCommand
    );


    Bridge.provide(
        "vision_status",
        visionStatus
    );


    Bridge.provide(
        "vision_last_result",
        visionLastResult
    );


    Bridge.provide(
        "vision_diagnostic_enabled",
        visionDiagnosticEnabled
    );




    Bridge.provide(
        "vision_auth_mac_word",
        visionAuthMacWord
    );


    Bridge.provide(
        "vision_auth_challenge",
        visionAuthChallenge
    );


    Bridge.provide(
        "vision_auth_ack",
        visionAuthAck
    );


    Bridge.provide(
        "vision_send_tile",
        visionSendTile
    );


    Bridge.provide(
        "vision_set_diagnostic",
        visionSetDiagnostic
    );


    // --------------------------------------------------------
    // MCU ready
    // --------------------------------------------------------

    digitalWrite(
        OUT5_GREEN_LED,
        HIGH
    );
}


// ============================================================
// Loop
// ============================================================

void loop() {

    const unsigned long now =
        millis();


    updateStartupReport(
        now
    );


    updateEnvironment(
        now
    );


    updateWaterCycle(
        now
    );


    updateProgramClock();


    updatePhysicalControls(
        now
    );


    updatePrograms();

    delay(5);
}
