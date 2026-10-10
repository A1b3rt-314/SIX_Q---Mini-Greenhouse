# SPDX-FileCopyrightText: 2026 Alberto Prestes
# SPDX-License-Identifier: 0BSD

"""
SIX_Q - Linux application  /////// VERSION 7 ///////

This file joins the pieces already validated independently:

- AHT20 environmental telemetry.
- Local WATER and LIGHT controls.
- Web WATER and LIGHT requests.
- Local SQL storage.
- Local WebUI dashboard.
- Nicla Vision V7 with optional diagnostics.

The basic rule is intentionally simple:

Interfaces request.
UNO Q decides.
Hardware acts only after that decision.

Physical controls and web controls have the same value.
Neither one is allowed to drive a relay directly.
"""

import os
import shutil
import threading
import time


from datetime import datetime, timezone
from pathlib import Path
from zoneinfo import ZoneInfo

from arduino.app_utils import App, Bridge
from arduino.app_bricks.dbstorage_sqlstore import SQLStore
from arduino.app_bricks.web_ui import WebUI

import vision_V7 as vision
import ai_analysis


# ============================================================
# SIX_Q - Configuration
# ============================================================

DATABASE_NAME = "six_q.db"

# Local installation time, independent of the container/browser timezone.
# Override SIXQ_TIMEZONE with an IANA timezone when installing elsewhere.
LOCAL_TIMEZONE_NAME = os.environ.get("SIXQ_TIMEZONE", "America/Montevideo")
LOCAL_TIMEZONE = ZoneInfo(LOCAL_TIMEZONE_NAME)

CAPTURE_ROOT = Path("/app/captures")
LATEST_MOSAIC = Path("/app/assets/latest_mosaic.png")

# The MCU reads the environment every 2 seconds so the dashboard feels alive.
#
# SQL does not need one row every 2 seconds.
# Thirty seconds is enough for environmental history in this prototype
# and avoids filling storage with thousands of almost identical samples.
ENVIRONMENT_DATABASE_INTERVAL = 30.0


# ------------------------------------------------------------
# USER capture rules
# ------------------------------------------------------------

# A complete capture generates 20 raw RGB565 tiles, 20 PNG tiles,
# one mosaic and metadata.
#
# The capture button is therefore not unrestricted.
#
# Current USER rules:
#
# - only one capture at a time
# - 120 second cooldown after an accepted request
# - maximum 3 accepted USER captures in a rolling 60 minute window
#
# TIMER and ANOMALY triggers will have their own rules later.

USER_CAPTURE_COOLDOWN = 120.0
USER_CAPTURE_WINDOW = 3600.0
USER_CAPTURE_MAX = 3


# ============================================================
# Arduino App Lab Bricks
# ============================================================

ui = WebUI()

db = SQLStore(
    DATABASE_NAME
)


# ============================================================
# SQL schema
# ============================================================

# Measurements and events are intentionally separated.
#
# environment_samples answers:
# "What was the environment like?"
#
# system_events answers:
# "What happened?"
#
# Mixing both into one table would eventually create a large table
# full of unrelated and empty columns.

db.create_table(
    "environment_samples",
    {
        "id": "INTEGER PRIMARY KEY",
        "timestamp_utc": "TEXT",
        "temperature": "REAL",
        "humidity": "REAL",
    }
)


db.create_table(
    "system_events",
    {
        "id": "INTEGER PRIMARY KEY",
        "timestamp_utc": "TEXT",
        "subsystem": "TEXT",
        "event": "TEXT",
        "source": "TEXT",
        "details": "TEXT",
    }
)


# ============================================================
# Shared runtime state
# ============================================================

state_lock = threading.Lock()
bridge_lock = threading.Lock()
capture_rule_lock = threading.Lock()
ai_rule_lock = threading.Lock()


state = {
    "sensor_ok": False,

    "temperature": None,
    "humidity": None,

    "water": False,
    "light": False,

    "vision": "IDLE",

    "capture_busy": False,
    "capture_message": "No USER capture requested yet.",

    "ai_busy": False,

    "last_image_available": (
        LATEST_MOSAIC.exists()
    ),

    "last_capture_utc": None,
}


last_environment_database_write = 0.0

user_capture_times = []

capture_cooldown_until_monotonic = 0.0
capture_cooldown_until_epoch_ms = 0


# ============================================================
# Time helpers
# ============================================================

def utc_now():
    return datetime.now(
        timezone.utc
    )


def utc_iso():
    return utc_now().isoformat()


# ============================================================
# Logging
# ============================================================

def log(message):
    print(
        message,
        flush=True
    )


# ============================================================
# Bridge serialization
# ============================================================

def bridge_call(
    method,
    *args
):
    """
    Linux can receive a web request while Vision is also running.

    RouterBridge gains nothing from receiving overlapping calls from
    different Python threads.

    A small lock keeps each individual Linux -> MCU call serialized.
    Physical MCU controls remain independent.
    """

    with bridge_lock:
        return Bridge.call(
            method,
            *args
        )


class VisionBridge:
    """
    Adapter for the Vision capture pipeline.

    vision.py expects an object with a .call() function.
    We give it exactly that while keeping Bridge calls serialized.
    """

    @staticmethod
    def call(
        method,
        *args
    ):
        return bridge_call(
            method,
            *args
        )


vision_bridge = VisionBridge()


# ============================================================
# SQL event helper
# ============================================================

def add_system_event(
    subsystem,
    event,
    source,
    details=""
):
    db.store(
        "system_events",
        {
            "timestamp_utc": utc_iso(),
            "subsystem": str(subsystem),
            "event": str(event),
            "source": str(source),
            "details": str(details),
        }
    )


# ============================================================
# USER capture accounting
# ============================================================

def prune_user_capture_window():
    global user_capture_times

    now = time.monotonic()

    cutoff = (
        now -
        USER_CAPTURE_WINDOW
    )

    user_capture_times = [
        timestamp
        for timestamp in user_capture_times
        if timestamp > cutoff
    ]


def capture_rule_state():

    prune_user_capture_window()

    now = time.monotonic()

    used = len(
        user_capture_times
    )

    remaining = max(
        0,
        USER_CAPTURE_MAX - used
    )

    cooldown_remaining = max(
        0,
        int(
            capture_cooldown_until_monotonic -
            now
        )
    )

    rate_reset_epoch_ms = 0

    if (
        used >= USER_CAPTURE_MAX
        and user_capture_times
    ):
        oldest = min(
            user_capture_times
        )

        seconds_until_reset = max(
            0.0,
            USER_CAPTURE_WINDOW -
            (now - oldest)
        )

        rate_reset_epoch_ms = int(
            (
                time.time() +
                seconds_until_reset
            ) * 1000
        )

    return {
        "capture_used_last_hour": used,

        "capture_remaining_last_hour":
            remaining,

        "capture_cooldown_remaining_seconds":
            cooldown_remaining,

        "capture_cooldown_until_epoch_ms":
            capture_cooldown_until_epoch_ms,

        "capture_rate_reset_epoch_ms":
            rate_reset_epoch_ms,
    }


# ============================================================
# Public dashboard state
# ============================================================

def public_state():

    with state_lock:
        payload = dict(
            state
        )

    payload.update(
        capture_rule_state()
    )

    # UNO Q owns absolute time.
    #
    # The browser may format this timestamp for display,
    # but it does not become the system clock authority.
    payload["server_time_utc"] = utc_iso()
    payload["server_timezone"] = LOCAL_TIMEZONE_NAME

    if payload[
        "last_image_available"
    ]:

        try:
            version = (
                LATEST_MOSAIC
                .stat()
                .st_mtime_ns
            )

        except OSError:
            version = int(
                time.time() *
                1000
            )

        # We always expose one filename:
        #
        # latest_mosaic.png
        #
        # The query value prevents the browser from showing an old
        # cached version after a new capture finishes.
        payload["last_image_url"] = (
            f"latest_mosaic.png?v={version}"
        )

    else:
        payload["last_image_url"] = None

    return payload


def send_state(
    room=None
):

    ui.send_message(
        "state_update",
        public_state(),
        room=room
    )


# ============================================================
# MCU -> Linux: AHT20
# ============================================================

def sensor_status(
    status: int
):

    ready = bool(
        status
    )

    with state_lock:
        state["sensor_ok"] = ready

    log(
        "SENSOR | AHT20 | "
        + (
            "READY"
            if ready
            else "NOT AVAILABLE"
        )
    )

    send_state()


def environment_sample(
    temperature: float,
    humidity: float
):

    global last_environment_database_write

    temperature = float(
        temperature
    )

    humidity = float(
        humidity
    )

    with state_lock:

        state["temperature"] = (
            temperature
        )

        state["humidity"] = (
            humidity
        )

    now = time.monotonic()

    # Live information and stored information do not need the same rate.
    #
    # The UI receives every measurement.
    # SQL receives one measurement every 30 seconds.

    if (
        now -
        last_environment_database_write
        >=
        ENVIRONMENT_DATABASE_INTERVAL
    ):

        db.store(
            "environment_samples",
            {
                "timestamp_utc":
                    utc_iso(),

                "temperature":
                    temperature,

                "humidity":
                    humidity,
            }
        )

        last_environment_database_write = (
            now
        )

    log(
        "ENVIRONMENT | "
        f"T={temperature:.2f} C | "
        f"H={humidity:.2f} %"
    )

    send_state()


# ============================================================
# MCU -> Linux: authoritative relay state
# ============================================================

def control_state(
    water: int,
    light: int
):

    with state_lock:

        state["water"] = bool(
            water
        )

        state["light"] = bool(
            light
        )

    send_state()


# ============================================================
# MCU -> Linux: physical control event
# ============================================================

def physical_control_event(
    subsystem: int,
    requested_state: int,
    accepted: int,
    actual_state: int
):

    subsystem_name = (
        "WATER"
        if int(subsystem) == 1
        else "LIGHT"
    )

    requested = bool(
        requested_state
    )

    accepted = bool(
        accepted
    )

    actual = bool(
        actual_state
    )

    with state_lock:

        if subsystem_name == "WATER":

            state["water"] = (
                actual
            )

        else:

            state["light"] = (
                actual
            )

    add_system_event(
        subsystem_name,

        (
            "STATE_ON"
            if actual
            else "STATE_OFF"
        ),

        "PHYSICAL",

        (
            f"requested="
            f"{'ON' if requested else 'OFF'}; "
            f"accepted={int(accepted)}"
        )
    )

    log(
        f"CONTROL | "
        f"{subsystem_name} | "
        f"PHYSICAL | "
        f"REQUEST="
        f"{'ON' if requested else 'OFF'} | "
        f"{'ACCEPTED' if accepted else 'REJECTED'} | "
        f"ACTUAL="
        f"{'ON' if actual else 'OFF'}"
    )

    send_state()


# ============================================================
# Register MCU notifications
# ============================================================

Bridge.provide(
    "sensor_status",
    sensor_status
)

Bridge.provide(
    "environment_sample",
    environment_sample
)

Bridge.provide(
    "control_state",
    control_state
)

Bridge.provide(
    "physical_control_event",
    physical_control_event
)


# ============================================================
# Read current control state
# ============================================================

def read_control_state():

    try:

        water = int(
            bridge_call(
                "get_water_state"
            )
        )

        light = int(
            bridge_call(
                "get_light_state"
            )
        )

        programs = {
            "water_program": int(bridge_call("get_water_program")),
            "light_program": int(bridge_call("get_light_program")),
            "water_used": int(bridge_call("get_water_used")),
        }

        with state_lock:
            state.update(programs)


            state["water"] = bool(
                water
            )

            state["light"] = bool(
                light
            )

        return True

    except Exception as error:

        log(
            "CONTROL | "
            f"STATE READ ERROR | {error}"
        )

        return False


# ============================================================
# Web -> UNO Q control
# ============================================================

def handle_web_control(
    subsystem,
    desired_state
):

    subsystem = (
        str(subsystem)
        .upper()
    )

    if subsystem not in (
        "WATER",
        "LIGHT"
    ):
        return

    desired = (
        1
        if bool(desired_state)
        else 0
    )

    method = (
        "request_water"
        if subsystem == "WATER"
        else "request_light"
    )

    try:

        # The web interface does not get a privileged control path.
        #
        # Physical and web requests call the exact same MCU rule
        # function. Only their source label is different for logging.

        accepted = bool(
            int(
                bridge_call(
                    method,
                    desired
                )
            )
        )

        # Never assume that a relay changed because we asked.
        #
        # Read the authoritative MCU state after the request.

        read_control_state()

        with state_lock:

            actual = (
                state["water"]
                if subsystem == "WATER"
                else state["light"]
            )

        add_system_event(
            subsystem,

            (
                "STATE_ON"
                if actual
                else "STATE_OFF"
            ),

            "WEB",

            (
                f"requested="
                f"{'ON' if desired else 'OFF'}; "
                f"accepted={int(accepted)}"
            )
        )

        log(
            f"CONTROL | {subsystem} | WEB | "
            f"REQUEST={'ON' if desired else 'OFF'} | "
            f"{'ACCEPTED' if accepted else 'REJECTED'} | "
            f"ACTUAL={'ON' if actual else 'OFF'}"
        )

    except Exception as error:

        add_system_event(
            subsystem,
            "REQUEST_ERROR",
            "WEB",
            str(error)
        )

        log(
            f"CONTROL | "
            f"{subsystem} | "
            f"WEB ERROR | "
            f"{error}"
        )

    send_state()


def on_water_request(
    sid,
    data
):

    desired = bool(
        (data or {})
        .get(
            "on",
            False
        )
    )

    handle_web_control(
        "WATER",
        desired
    )


def on_light_request(
    sid,
    data
):

    desired = bool(
        (data or {})
        .get(
            "on",
            False
        )
    )

    handle_web_control(
        "LIGHT",
        desired
    )


# ============================================================
# USER capture rule
# ============================================================

def user_capture_allowed():

    prune_user_capture_window()

    now = time.monotonic()

    with state_lock:
        busy = bool(
            state[
                "capture_busy"
            ]
        )

    if busy:

        return (
            False,
            "Vision is busy."
        )

    if (
        now <
        capture_cooldown_until_monotonic
    ):

        remaining = int(
            capture_cooldown_until_monotonic -
            now
        )

        return (
            False,
            (
                "Capture cooldown active "
                f"({remaining}s remaining)."
            )
        )

    if (
        len(user_capture_times)
        >=
        USER_CAPTURE_MAX
    ):

        return (
            False,
            (
                "USER capture limit reached: "
                "3 in the last 60 minutes."
            )
        )

    return (
        True,
        "Accepted."
    )


# ============================================================
# Find a finished mosaic, including captures from previous installations
# ============================================================

def find_capture_mosaic(
    capture_id
):

    candidates = []

    for pattern in (
        f"sixqv7_*_{capture_id}/mosaico.png",
        f"sixqv6_*_{capture_id}/mosaico.png",
        f"sixqv5_*_{capture_id}/mosaico.png",
        f"copilotv4_*_{capture_id}/mosaico.png"
    ):

        candidates.extend(
            CAPTURE_ROOT.glob(
                pattern
            )
        )

    if not candidates:

        return None

    return max(
        candidates,
        key=lambda path:
            path.stat().st_mtime
    )


# ============================================================
# USER capture worker
# ============================================================

def run_user_capture():

    try:

        with state_lock:

            state["vision"] = (
                "STARTING"
            )

            state["capture_message"] = (
                "Preparing Nicla Vision."
            )

        send_state()

        # Vision V7 stays responsible for:
        #
        # AUTH
        # READY
        # SEND_TILE
        # TCP reception
        # RGB565 conversion
        # mosaic assembly
        #
        # main.py only decides WHEN that pipeline is allowed to start.

        manifest = (
            vision.capture_mosaic(
                vision_bridge
            )
        )

        if manifest.get(
            "complete"
        ):

            capture_id = manifest[
                "capture_id"
            ]

            mosaic_path = (
                find_capture_mosaic(
                    capture_id
                )
            )

            if mosaic_path is None:

                raise RuntimeError(
                    "Vision completed but mosaico.png was not found."
                )

            # Dashboard policy:
            #
            # show one image.
            # The latest one.
            #
            # A historical image browser belongs to the later
            # timeline feature, not to tonight's prototype.

            shutil.copy2(
                mosaic_path,
                LATEST_MOSAIC
            )

            finished = (
                manifest.get(
                    "finished_utc"
                )
                or
                utc_iso()
            )

            with state_lock:

                state["last_image_available"] = (
                    True
                )

                state["last_capture_utc"] = (
                    finished
                )

                state["vision"] = (
                    "READY"
                )

                state["capture_message"] = (
                    "USER capture completed. "
                    + {"screen_pass": "No visual anomaly detected.",
                       "suspect": "WARNING: tile quality remains unconfirmed after bounded recapture.",
                       "acquisition_or_integrity_failed": "WARNING: acquisition/transfer failed; image kept as evidence.",
                       "not_checked": "Visual screening disabled."
                      }.get(manifest.get("quality_status"), "Image quality unavailable.")
                )

            add_system_event(
                "VISION",
                "CAPTURE_COMPLETE",
                "USER",
                f"capture_id={capture_id}; quality={manifest.get('quality_status', 'unknown')}; integrity={manifest.get('integrity_status', 'unavailable')}"
            )

        else:

            error = (
                manifest.get(
                    "error"
                )
                or
                "Unknown Vision error."
            )

            with state_lock:

                state["vision"] = (
                    "ERROR"
                )

                state["capture_message"] = (
                    str(error)
                )

            add_system_event(
                "VISION",
                "CAPTURE_ERROR",
                "USER",
                str(error)
            )

    except Exception as error:

        with state_lock:

            state["vision"] = (
                "ERROR"
            )

            state["capture_message"] = (
                str(error)
            )

        add_system_event(
            "VISION",
            "CAPTURE_ERROR",
            "USER",
            str(error)
        )

        log(
            "VISION | "
            f"USER CAPTURE ERROR | "
            f"{error}"
        )

    finally:

        with state_lock:

            state["capture_busy"] = (
                False
            )

        # Important:
        #
        # We do NOT start another 120 second timer here.
        #
        # The cooldown started when the request was accepted.
        # Tile reception, mosaic assembly and web update happen inside
        # that same window.

        send_state()


# ============================================================
# Web USER trigger
# ============================================================

def on_user_capture(
    sid,
    data
):

    global capture_cooldown_until_monotonic
    global capture_cooldown_until_epoch_ms

    with capture_rule_lock:

        allowed, reason = (
            user_capture_allowed()
        )

        if not allowed:

            add_system_event(
                "VISION",
                "CAPTURE_DENIED",
                "USER",
                reason
            )

            with state_lock:

                state[
                    "capture_message"
                ] = reason

            log(
                "VISION | "
                f"USER CAPTURE DENIED | "
                f"{reason}"
            )

            send_state()

            return

        now = time.monotonic()

        # We count accepted USER capture starts.
        #
        # Those are the requests capable of consuming disk space.
        # Rejected clicks do not create image files and therefore
        # do not consume the storage quota.

        user_capture_times.append(
            now
        )

        capture_cooldown_until_monotonic = (
            now +
            USER_CAPTURE_COOLDOWN
        )

        capture_cooldown_until_epoch_ms = int(
            (
                time.time() +
                USER_CAPTURE_COOLDOWN
            )
            * 1000
        )

        with state_lock:

            state["capture_busy"] = (
                True
            )

            state["vision"] = (
                "QUEUED"
            )

            state["capture_message"] = (
                "USER capture accepted."
            )

        add_system_event(
            "VISION",
            "CAPTURE_REQUESTED",
            "USER",
            "Accepted by USER capture rules."
        )

        send_state()

        threading.Thread(
            target=run_user_capture,
            daemon=True,
            name="sixq-user-capture"
        ).start()


# ============================================================
# Manual AI image analysis
# ============================================================

def build_ai_overlay_text(
    result
):

    analysis = (
        result.get(
            "analysis",
            {}
        )
        or
        {}
    )

    plant = bool(
        analysis.get(
            "plant",
            False
        )
    )

    plant_number = int(
        analysis.get(
            "plant_number",
            0
        )
        or
        0
    )

    health = str(
        analysis.get(
            "health",
            "unknown"
        )
    ).upper()

    development = str(
        analysis.get(
            "development_state",
            "Unknown"
        )
    ).upper()

    substrate = str(
        analysis.get(
            "substrate",
            "Unknown"
        )
    ).upper()

    if not plant:

        return (
            "NO PLANT DETECTED"
            " · "
            f"SUBSTRATE {substrate}"
        )

    plant_label = (
        "PLANT"
        if plant_number == 1
        else "PLANTS"
    )

    return (
        f"{plant_number} {plant_label}"
        " · "
        f"{health}"
        " · "
        f"{development}"
        " · "
        f"SUBSTRATE {substrate}"
    )


def send_ai_status(
    sid,
    status,
    overlay_text=""
):

    ui.send_message(
        "ai_analysis_status",
        {
            "status": str(status),
            "overlay_text": str(overlay_text),
        },
        room=sid
    )


def run_ai_analysis(
    sid
):

    try:

        send_ai_status(
            sid,
            "analyzing",
            "ANALYZING IMAGE..."
        )

        log(
            "AI | USER ANALYSIS | STARTING"
        )

        result = (
            ai_analysis.analyze_image(
                str(
                    LATEST_MOSAIC
                )
            )
        )

        overlay_text = (
            build_ai_overlay_text(
                result
            )
        )

        usage = (
            result.get(
                "usage",
                {}
            )
            or
            {}
        )

        log(
            "AI | USER ANALYSIS | COMPLETE | "
            f"{overlay_text} | "
            f"TOKENS={usage.get('total_tokens', 'unknown')}"
        )

        add_system_event(
            "AI",
            "ANALYSIS_COMPLETE",
            "USER",
            overlay_text
        )

        send_ai_status(
            sid,
            "complete",
            overlay_text
        )

    except Exception as error:

        log(
            "AI | USER ANALYSIS | ERROR | "
            f"{error}"
        )

        add_system_event(
            "AI",
            "ANALYSIS_ERROR",
            "USER",
            str(error)
        )

        send_ai_status(
            sid,
            "error",
            "AI ANALYSIS FAILED"
        )

    finally:

        with state_lock:

            state[
                "ai_busy"
            ] = False

        send_state()


def on_ai_analysis_request(
    sid,
    data
):

    with ai_rule_lock:

        with state_lock:

            busy = bool(
                state[
                    "ai_busy"
                ]
            )

        if busy:

            send_ai_status(
                sid,
                "error",
                "AI ANALYSIS ALREADY RUNNING"
            )

            return

        if not LATEST_MOSAIC.exists():

            send_ai_status(
                sid,
                "error",
                "NO IMAGE AVAILABLE"
            )

            return

        with state_lock:

            state[
                "ai_busy"
            ] = True

        add_system_event(
            "AI",
            "ANALYSIS_REQUESTED",
            "USER",
            "latest_mosaic.png"
        )

        threading.Thread(
            target=run_ai_analysis,
            args=(sid,),
            daemon=True,
            name="sixq-ai-analysis"
        ).start()


# ============================================================
# Dashboard connection
# ============================================================

def on_ui_connect(
    sid
):

    # A newly opened dashboard gets an immediate snapshot.
    #
    # It should not have to wait for the next AHT20 reading
    # before knowing the current WATER and LIGHT state.

    read_control_state()

    send_state(
        room=sid
    )


def sync_program_clock():
    # Sample after acquiring the lock, so a capture cannot leave a queued
    # synchronization carrying an old timestamp.
    with bridge_lock:
        local = datetime.now(LOCAL_TIMEZONE)
        Bridge.call("set_program_clock", local.toordinal(), local.hour * 3600 + local.minute * 60 + local.second)


def on_program_request(sid, data):
    if not isinstance(data, dict):
        return
    subsystem = data.get("subsystem")
    hours = data.get("hours")
    allowed = {"water": (4, 6, 8, 12, 24), "light": (6, 8, 12, 18)}
    if subsystem not in allowed or type(hours) is not int or hours not in allowed[subsystem]:
        return
    try:
        sync_program_clock()
        accepted = bool(bridge_call(f"set_{subsystem}_program", hours))
        log(f"PROGRAM | {subsystem} | hours={hours} | accepted={accepted}")
    except Exception as error:
        log(f"PROGRAM | ERROR | {error}")
    read_control_state()
    send_state()


def monitor_programs():
    while True:
        try:
            sync_program_clock()
            read_control_state()
            send_state()
        except Exception as error:
            log(f"PROGRAM | SYNC ERROR | {error}")
        time.sleep(2)


# ============================================================
# Register WebUI requests
# ============================================================

ui.on_message("program_request", on_program_request)

ui.on_connect(
    on_ui_connect
)

ui.on_message(
    "water_request",
    on_water_request
)

ui.on_message(
    "light_request",
    on_light_request
)

ui.on_message(
    "user_capture",
    on_user_capture
)


ui.on_message(
    "ai_analysis_request",
    on_ai_analysis_request
)


# ============================================================
# Startup
# ============================================================

def startup():

    CAPTURE_ROOT.mkdir(
        parents=True,
        exist_ok=True
    )

    LATEST_MOSAIC.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    add_system_event(
        "SYSTEM",
        "APP_START",
        "SYSTEM",
        "SIX_Q Linux application started."
    )

    # Give the MCU a moment to finish initialization.
    time.sleep(2)

    threading.Thread(target=monitor_programs, daemon=True, name="sixq-programs").start()
    read_control_state()
    send_state()

    log(
        "SIX_Q | Linux application ready"
    )

    log(
        "SIX_Q | Web dashboard -> port 7000"
    )

    log(
        "SIX_Q | Nicla image data -> TCP port 5000"
    )


threading.Thread(
    target=startup,
    daemon=True,
    name="sixq-startup"
).start()


# ============================================================
# Run App Lab
# ============================================================

App.run()
