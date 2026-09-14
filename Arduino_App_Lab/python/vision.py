# SPDX-FileCopyrightText: 2026 Alberto Prestes
# SPDX-License-Identifier: 0BSD

"""
SIX_Q - Vision module

This module keeps the image pipeline that was already validated before the
rest of SIX_Q was integrated. Its job is deliberately narrow:

- authenticate the Nicla over the control channel;
- request one 320 x 320 tile at a time;
- receive image data over TCP;
- rebuild the 5 x 4 mosaic;
- save raw tiles, PNG tiles, the final mosaic and a manifest.

Architecture rule:
UNO Q decides when a capture is allowed to happen. Nicla only executes the
requested image operation and reports its state. This module therefore does
not own irrigation, lighting, schedules or dashboard rules.

Important integration rule:
This pipeline already worked before the dashboard/SQL integration. Keep its
protocol values and tile geometry stable unless a separate test proves that a
change is necessary.
"""

import hashlib
import hmac
import json
import secrets
import socket
import struct
import threading
import time

from datetime import datetime, timezone
from pathlib import Path

from PIL import Image

from secrets_auth import AUTH_SHARED_KEY


# ============================================================
# Network and image geometry
# ============================================================

# TCP port 5000 is dedicated to Nicla -> UNO Q image transfer.
#
# Control/status travels over I2C.
# Image payloads travel over TCP.
#
# Keeping those two jobs separated avoids trying to push large image
# buffers through a control bus that was never meant for that job.

HOST, PORT = "0.0.0.0", 5000

OUTPUT_DIR = Path(
    "/app/captures"
)

FULL_W, FULL_H = (
    1600,
    1200
)

TILE_W, TILE_H = (
    320,
    320
)

TILE_BYTES = (
    TILE_W *
    TILE_H *
    2
)


# ============================================================
# Vision protocol
# ============================================================

PROTOCOL = 5

# Keep this value unchanged for now.
# It is part of the authentication agreement between UNO Q and Nicla,
# not a user-visible version label.

AUTH_CONTEXT = (
    b"SIXQ_V4"
)


CMD_AUTH_CHALLENGE, \
CMD_AUTH_RESPONSE, \
CMD_AUTH_ACK = (
    0x01,
    0x02,
    0x03
)


CMD_SEND_TILE, \
CMD_STATUS = (
    0x11,
    0xF0
)


# ============================================================
# Nicla state machine
# ============================================================

ST_BOOT, \
ST_AUTH, \
ST_READY, \
ST_CAPTURE, \
ST_SEND, \
ST_AUTH_REJECTED = (
    0,
    1,
    2,
    3,
    4,
    5
)


STATE_NAMES = {

    0:
        "BOOT",

    1:
        "AUTH",

    2:
        "READY",

    3:
        "CAPTURE",

    4:
        "SEND",

    5:
        "AUTH REJECTED",

    225:
        "CAMERA INIT ERROR",

    226:
        "TCP CONNECT ERROR",

    227:
        "WIFI ERROR",

    228:
        "ZOOM ERROR",

    231:
        "CAPTURE ERROR",

    232:
        "BUFFER ERROR",

    233:
        "TCP WRITE ERROR",

    234:
        "EXPOSURE ERROR",

    235:
        "NETWORK CONFIG ERROR",
}


# ============================================================
# Mosaic grid
# ============================================================

# The grid is shared with the Nicla firmware and must stay
# identical on both sides.
#
# 320 x 320 was chosen because it is the largest tile size
# already proven stable in repeated complete-mosaic tests.
#
# Horizontal positions:
#
#     0
#     320
#     640
#     960
#     1280
#
# Vertical positions:
#
#     0
#     320
#     640
#     880
#
# The last row deliberately begins at y=880.
#
# That creates a small overlap, but it lets us cover the complete
# 1200-pixel sensor height while keeping the same proven 320 x 320
# capture size.
#
# We prefer a small known overlap over introducing another tile
# geometry only to remove it.

GRID_X = (
    0,
    320,
    640,
    960,
    1280
)

GRID_Y = (
    0,
    320,
    640,
    880
)


# ============================================================
# Exceptions
# ============================================================

class I2CResponseError(
    RuntimeError
):
    """
    Raised when the latest control-channel response
    cannot be trusted.
    """


# ============================================================
# Logging
# ============================================================

def log(
    message
):

    print(
        message,
        flush=True
    )


# ============================================================
# Tile positions
# ============================================================

def positions():

    return [

        (x, y)

        for y in GRID_Y

        for x in GRID_X
    ]


# ============================================================
# Control channel - status polling
# ============================================================

# I2C carries control and status only.
#
# Image bytes never travel on this channel.
#
# The UNO Q asks.
# Nicla reports.
# UNO Q decides what happens next.

def poll_response(
    bridge
):

    if not bridge.call(
        "vision_poll_response"
    ):

        raise I2CResponseError(
            "I2C response missing or invalid "
            "(see UNO Q/Nicla serial)"
        )


def read_status(
    bridge
):

    poll_response(
        bridge
    )


    state = int(
        bridge.call(
            "vision_status"
        )
    )


    if state in (
        253,
        255
    ):

        raise I2CResponseError(
            "I2C response is not a valid STATUS frame"
        )


    if state == ST_AUTH_REJECTED:

        raise RuntimeError(
            "AUTH rejected by Nicla"
        )


    if state >= 225:

        raise RuntimeError(
            STATE_NAMES.get(
                state,
                f"NICLA ERROR {state}"
            )
        )


    return state


def wait_status(
    bridge,
    expected,
    timeout
):

    deadline = (
        time.monotonic() +
        timeout
    )


    last_state = None
    last_error = None
    failures = 0


    while (
        time.monotonic() <
        deadline
    ):

        try:

            state = read_status(
                bridge
            )


        except I2CResponseError as error:

            failures += 1

            last_error = str(
                error
            )


            # Repeated temporary I2C failures are not logged on
            # every single poll.
            #
            # That would produce a wall of text that hides the
            # information we actually need when diagnosing a failure.

            if (
                failures == 1
                or
                failures % 25 == 0
            ):

                log(
                    f"I2C | Waiting for "
                    f"{STATE_NAMES.get(expected, expected)} | "
                    f"poll failures={failures} | "
                    f"{error}"
                )


            time.sleep(
                0.2
            )

            continue


        if failures:

            log(
                f"I2C | RECOVERED after "
                f"{failures} failed polls"
            )

            failures = 0


        last_error = None


        if state != last_state:

            log(
                "NICLA | "
                f"{STATE_NAMES.get(state, state)}"
            )


        last_state = state


        if state == expected:

            return


        time.sleep(
            0.2
        )


    detail = (
        f"last state="
        f"{STATE_NAMES.get(last_state, last_state)}"
    )


    if last_error:

        detail += (
            f"; {last_error}; "
            "check Nicla serial for the last startup phase or reset"
        )


    raise TimeoutError(
        f"Timed out waiting for "
        f"{STATE_NAMES.get(expected, expected)} "
        f"after {timeout}s ({detail})"
    )


# ============================================================
# Authentication
# ============================================================

def authenticate(
    bridge,
    timeout=10.0
):

    """
    Authenticate the Nicla before image commands are accepted.

    UNO Q creates a fresh random nonce.

    Nicla answers with an HMAC-SHA256 value generated with the
    shared key.

    UNO Q independently calculates the expected value and compares
    both results.

    The shared key itself never travels over I2C.

    CRC and HMAC intentionally solve different problems:

    CRC:
        Was this frame accidentally corrupted?

    HMAC:
        Did this answer come from a device that knows the
        expected shared secret?
    """


    nonce = (
        secrets.token_bytes(
            8
        )
    )


    nonce_hi, \
    nonce_lo = (
        struct.unpack(
            ">II",
            nonce
        )
    )


    if not bridge.call(
        "vision_auth_challenge",
        nonce_hi,
        nonce_lo
    ):

        raise RuntimeError(
            "AUTH_CHALLENGE could not be sent over I2C"
        )


    deadline = (
        time.monotonic() +
        timeout
    )


    while (
        time.monotonic() <
        deadline
    ):

        try:

            poll_response(
                bridge
            )

        except I2CResponseError:

            time.sleep(
                0.05
            )

            continue


        if int(
            bridge.call(
                "vision_response_command"
            )
        ) == CMD_AUTH_RESPONSE:


            words = [

                int(
                    bridge.call(
                        "vision_auth_mac_word",
                        i
                    )
                )

                for i in range(4)
            ]


            mac = b"".join(

                struct.pack(
                    ">I",
                    word
                )

                for word in words
            )


            break


        time.sleep(
            0.05
        )


    else:

        raise TimeoutError(
            "Timed out waiting for AUTH_RESPONSE"
        )


    expected = hmac.new(

        AUTH_SHARED_KEY,

        nonce +
        AUTH_CONTEXT,

        hashlib.sha256

    ).digest()[:16]


    ok = (
        hmac.compare_digest(
            mac,
            expected
        )
    )


    if not bridge.call(
        "vision_auth_ack",
        ok
    ):

        raise RuntimeError(
            "AUTH_ACK could not be sent over I2C"
        )


    if not ok:

        raise RuntimeError(
            "AUTH rejected: "
            "Nicla does not hold the shared key"
        )


    log(
        "AUTH | ACCEPTED, "
        "waiting for camera/WiFi warmup"
    )


    wait_status(
        bridge,
        ST_READY,
        90
    )


# ============================================================
# TCP image receiver
# ============================================================

# TCP is the image-data channel.
#
# One recv() call is never assumed to contain the complete tile.
#
# TCP gives us a byte stream, not message boundaries.
#
# receive_exact() therefore keeps reading until the requested
# number of bytes has actually arrived.

def receive_exact(
    conn,
    count,
    deadline,
    progress_label=None
):

    data = bytearray()


    next_progress = (
        time.monotonic() +
        5
    )


    while (
        len(data) <
        count
    ):

        now = (
            time.monotonic()
        )


        remaining = (
            deadline -
            now
        )


        if remaining <= 0:

            raise TimeoutError(
                f"{progress_label or 'Tile'} "
                f"timeout at "
                f"{len(data)}/{count} bytes"
            )


        if (
            progress_label
            and
            now >= next_progress
        ):

            log(
                f"VISION | "
                f"{progress_label} | "
                f"RECEIVED "
                f"{len(data)}/{count} bytes"
            )


            next_progress = (
                now +
                5
            )


        conn.settimeout(
            min(
                0.25,
                remaining
            )
        )


        try:

            chunk = conn.recv(
                min(
                    16384,
                    count -
                    len(data)
                )
            )


        except socket.timeout:

            continue


        if not chunk:

            raise ConnectionError(
                f"Tile closed at "
                f"{len(data)}/{count} bytes"
            )


        data.extend(
            chunk
        )


    return bytes(
        data
    )


# ============================================================
# TCP header reader
# ============================================================

def receive_line(
    conn,
    deadline,
    progress_label=None
):

    data = bytearray()


    while (
        len(data) <
        96
    ):

        value = receive_exact(
            conn,
            1,
            deadline,
            progress_label
        )


        if value == b"\n":

            return (
                data
                .decode("ascii")
                .rstrip("\r")
            )


        data.extend(
            value
        )


    raise ValueError(
        "Tile header line exceeds 95 bytes"
    )


# ============================================================
# Receive one tile
# ============================================================

def receive_tile(
    conn,
    capture_id,
    capture_seq,
    tile_id,
    x,
    y
):

    deadline = (
        time.monotonic() +
        60
    )


    line = lambda: receive_line(
        conn,
        deadline,
        f"TILE {tile_id} HEADER next byte"
    )


    # Keep this protocol identifier unchanged for now.
    #
    # "SIXQ_TILE_V4" is exchanged with the Nicla itself.
    # It is not just a display label.

    if (
        line(),
        line()
    ) != (
        "SIXQ_TILE_V4",
        "RGB565"
    ):

        raise ValueError(
            "Unexpected tile protocol"
        )


    width, \
    height, \
    size = (

        int(line()),
        int(line()),
        int(line())
    )


    received_capture, \
    received_seq = (

        int(line()),
        int(line())
    )


    received_tile, \
    actual_x, \
    actual_y = (

        int(line()),
        int(line()),
        int(line())
    )


    captured_ms, \
    send_duration_ms = (

        int(line()),
        int(line())
    )


    if line() != "":

        raise ValueError(
            "Missing tile header terminator"
        )


    if (
        width,
        height,
        size
    ) != (
        TILE_W,
        TILE_H,
        TILE_BYTES
    ):

        raise ValueError(
            f"Invalid tile dimensions "
            f"{width}x{height}/{size}"
        )


    # Identity and coordinates are checked before accepting
    # the tile.
    #
    # A perfectly valid image placed in the wrong location
    # would still silently corrupt the final mosaic.

    if (
        received_capture,
        received_seq,
        received_tile,
        actual_x,
        actual_y
    ) != (
        capture_id,
        capture_seq,
        tile_id,
        x,
        y
    ):

        raise ValueError(
            "Tile identity or coordinates "
            "do not match request"
        )


    log(
        f"VISION | "
        f"TILE {tile_id} HEADER OK | "
        f"EXPECTING {TILE_BYTES} bytes"
    )


    raw = receive_exact(

        conn,

        TILE_BYTES,

        deadline,

        f"TILE {tile_id} RGB565"
    )


    log(
        f"VISION | "
        f"TILE {tile_id} PAYLOAD COMPLETE | "
        f"{len(raw)} bytes"
    )


    return (

        raw,

        {
            "tile_id":
                tile_id,

            "x":
                x,

            "y":
                y,

            "sha256":
                hashlib.sha256(
                    raw
                ).hexdigest(),

            "capture_completed_ms":
                captured_ms,

            "send_duration_ms":
                send_duration_ms,
        }
    )


# ============================================================
# RGB565 conversion
# ============================================================

# Nicla sends the already validated RGB565 byte stream.
#
# Conversion is deliberately done on UNO Q instead of Nicla.
#
# The camera node should spend its limited resources capturing
# and transmitting the image.
#
# UNO Q has more memory and processing room for decoding,
# validation, storage and assembly.

def image_from_rgb565(
    raw
):

    rgb = bytearray(
        TILE_W *
        TILE_H *
        3
    )


    for index in range(
        TILE_W *
        TILE_H
    ):

        value = (

            raw[
                2 * index
            ]
            << 8

        ) | raw[
            2 * index + 1
        ]


        rgb[
            3 * index
        ] = (
            (
                value >> 11
            )
            &
            31
        ) << 3


        rgb[
            3 * index + 1
        ] = (
            (
                value >> 5
            )
            &
            63
        ) << 2


        rgb[
            3 * index + 2
        ] = (
            value &
            31
        ) << 3


    return Image.frombytes(

        "RGB",

        (
            TILE_W,
            TILE_H
        ),

        bytes(
            rgb
        )
    )


# ============================================================
# Capture metadata
# ============================================================

# The manifest is first written to a temporary file and then
# atomically replaces manifest.json.
#
# If power is lost or the application is interrupted during the
# write, this reduces the chance of leaving the last known
# manifest half-written.

def save_manifest(
    path,
    manifest
):

    temporary = (
        path /
        "manifest.tmp"
    )


    temporary.write_text(

        json.dumps(
            manifest,
            indent=2
        )
        +
        "\n",

        encoding="utf-8"
    )


    temporary.replace(
        path /
        "manifest.json"
    )


# ============================================================
# Full mosaic capture
# ============================================================

# UNO Q requests each tile individually.
#
# Nicla does not independently decide to capture the next tile.
#
# For every tile:
#
#     UNO Q requests.
#     Nicla captures.
#     Nicla sends.
#     Nicla returns to READY.
#     UNO Q verifies READY.
#     UNO Q requests the next tile.
#
# This keeps orchestration and decision-making on one side of
# the architecture.

def capture_mosaic(
    bridge,
    output_dir=OUTPUT_DIR
):

    # capture_id identifies the complete mosaic.

    capture_id = (
        secrets.randbelow(
            0x7FFFFFFF
        )
        +
        1
    )


    # capture_seq remains a separate protocol field so that a
    # future retry can keep the same capture identity without
    # changing the wire format.
    #
    # The current validated path starts at sequence zero.

    capture_seq = 0


    # UNO Q owns the absolute timestamp.
    #
    # Nicla reports only relative timing associated with its
    # capture and send operations.

    start = (
        datetime.now(
            timezone.utc
        )
    )


    # IMPORTANT:
    #
    # We are not renaming the capture directory in this step.
    #
    # "copilotv4_" is an internal historical filename prefix,
    # not a visible runtime log. We will clean metadata and
    # filenames separately after confirming this change works.

    directory = (

        Path(
            output_dir
        )

        /

        (
            f"copilotv4_"
            f"{start:%Y%m%dT%H%M%S_%fZ}_"
            f"{capture_id}"
        )
    )


    directory.mkdir(
        parents=True,
        exist_ok=False
    )


    grid = positions()


    manifest = {

        # This historical metadata label is also left untouched
        # in this first pass.
        #
        # First we remove the visible COPILOT V4 console prefix.
        # Metadata cleanup comes next.

        "variant":
            "Copilot V4 "
            "(SIX_Q Vision v1: "
            "AUTH + SEND_TILE, 320x320)",

        "protocol":
            PROTOCOL,

        "capture_id":
            capture_id,

        "capture_seq":
            capture_seq,

        "width":
            FULL_W,

        "height":
            FULL_H,

        "tile_count":
            len(grid),

        "complete":
            False,

        "received_tiles":
            [],

        "missing_tiles":
            list(
                range(
                    len(grid)
                )
            ),

        "error":
            None
    }


    save_manifest(
        directory,
        manifest
    )


    mosaic = Image.new(

        "RGB",

        (
            FULL_W,
            FULL_H
        )
    )


    try:

        # UNO Q is the TCP server.
        #
        # Nicla opens one connection per requested tile.
        #
        # Port 5000 is intentionally separated from the
        # local WebUI service.

        with socket.socket(
            socket.AF_INET,
            socket.SOCK_STREAM
        ) as server:


            server.setsockopt(

                socket.SOL_SOCKET,

                socket.SO_REUSEADDR,

                1
            )


            server.bind(
                (
                    HOST,
                    PORT
                )
            )


            server.listen(
                2
            )


            log(
                f"VISION | "
                f"LISTENING "
                f"{HOST}:{PORT}"
            )


            # No image command is accepted before the Nicla
            # has proven that it holds the expected shared key.

            authenticate(
                bridge
            )


            for (
                tile_id,
                (x, y)
            ) in enumerate(
                grid
            ):


                log(
                    f"VISION | "
                    f"REQUEST TILE "
                    f"{tile_id} "
                    f"({x},{y})"
                )


                if not bridge.call(

                    "vision_send_tile",

                    capture_id,

                    capture_seq,

                    tile_id,

                    x,

                    y
                ):

                    raise RuntimeError(
                        f"SEND_TILE "
                        f"{tile_id} "
                        "could not be sent over I2C"
                    )


                server.settimeout(
                    60
                )


                conn, address = (
                    server.accept()
                )


                with conn:

                    log(
                        f"VISION | "
                        f"TILE {tile_id} "
                        f"({x},{y}) "
                        f"FROM {address[0]}"
                    )


                    raw, metadata = (
                        receive_tile(

                            conn,

                            capture_id,

                            capture_seq,

                            tile_id,

                            x,

                            y
                        )
                    )


                image = (
                    image_from_rgb565(
                        raw
                    )
                )


                stem = (
                    f"tile_{tile_id:02d}_"
                    f"x{x:04d}_"
                    f"y{y:04d}"
                )


                # During prototype development we deliberately
                # keep both representations:
                #
                # .rgb565 = original bytes received from Nicla
                # .png    = decoded image
                #
                # The raw file is useful when we need to inspect
                # conversion or camera behavior without repeating
                # the physical capture.

                (
                    directory /
                    f"{stem}.rgb565"
                ).write_bytes(
                    raw
                )


                image.save(
                    directory /
                    f"{stem}.png"
                )


                # Each verified tile is placed at the coordinates
                # requested by the UNO Q.

                mosaic.paste(
                    image,
                    (
                        x,
                        y
                    )
                )


                metadata[
                    "file"
                ] = (
                    f"{stem}.png"
                )


                manifest[
                    "received_tiles"
                ].append(
                    metadata
                )


                manifest[
                    "missing_tiles"
                ].remove(
                    tile_id
                )


                # Save progress after each accepted tile.
                #
                # If the sequence fails later, we still know exactly
                # what arrived and what was still missing.

                save_manifest(
                    directory,
                    manifest
                )


                log(
                    f"VISION | "
                    f"TILE {tile_id} "
                    f"SEND_DURATION_MS = "
                    f"{metadata['send_duration_ms']}"
                )


                # Do not request another tile until Nicla has
                # explicitly returned to READY.

                wait_status(
                    bridge,
                    ST_READY,
                    30
                )


        # A mosaic is considered complete only after every tile
        # was received, validated and assembled.

        mosaic.save(
            directory /
            "mosaico.png"
        )


        manifest[
            "complete"
        ] = True


        manifest[
            "mosaic"
        ] = (
            "mosaico.png"
        )


        save_manifest(
            directory,
            manifest
        )


        log(
            f"VISION | "
            f"COMPLETE "
            f"{directory / 'mosaico.png'}"
        )


    except Exception as error:

        manifest[
            "error"
        ] = str(
            error
        )


        # If at least one tile arrived correctly, keep the partial
        # mosaic.
        #
        # Failed captures are useful diagnostic information during
        # prototype development and should not disappear silently.

        if manifest[
            "received_tiles"
        ]:

            mosaic.save(
                directory /
                "mosaico_partial.png"
            )


        log(
            f"VISION | "
            f"ABORTED "
            f"{len(manifest['received_tiles'])}/"
            f"{len(grid)} | "
            f"{error}"
        )


    finally:

        # finished_utc is written whether the capture succeeded
        # or failed.
        #
        # That gives the UNO Q a closed record for every attempt.

        manifest[
            "finished_utc"
        ] = (
            datetime.now(
                timezone.utc
            )
            .isoformat()
        )


        save_manifest(
            directory,
            manifest
        )


    return manifest


# ============================================================
# Standalone diagnostic entry point
# ============================================================

# vision.py normally gets imported by the integrated main.py.
#
# This standalone entry point is kept because it is useful for
# isolating Vision during diagnostics.
#
# Running vision.py directly waits 10 seconds and performs one
# capture using the same validated pipeline.
#
# Importing vision.py does NOT execute this block.

def main():

    from arduino.app_utils import (
        App,
        Bridge
    )


    def worker():

        time.sleep(
            10
        )


        try:

            capture_mosaic(
                Bridge
            )


        except Exception as error:

            log(
                f"VISION | "
                f"STARTUP ERROR | "
                f"{error}"
            )


    threading.Thread(

        target=worker,

        daemon=True

    ).start()


    App.run()


if __name__ == "__main__":

    main()