"""V7 fixed sensor: source CRC, chromatic screen and preserved raw evidence."""
import hashlib
import hmac
import secrets
import socket
import struct
import time
import zlib

from PIL import Image

from pathlib import Path
import json

# ============================================================
# Geometría y protocolo compartidos con la Nicla V7.
# ============================================================

FULL_W, FULL_H = 1600, 1200
TILE_W, TILE_H = 320, 320
TILE_BYTES = TILE_W * TILE_H * 2

GRID_X = (0, 320, 640, 960, 1280)
GRID_Y = (0, 320, 640, 880)

PROTOCOL = 9  # Nicla, bridge y Python V7 deben usar la misma revisión.
AUTH_CONTEXT = b"SIXQ_V4"  # acuerdo de clave compartida; no es una etiqueta de versión

CMD_AUTH_CHALLENGE, CMD_AUTH_RESPONSE, CMD_AUTH_ACK = 0x01, 0x02, 0x03
CMD_SEND_TILE = 0x11
CMD_SET_DIAGNOSTIC = 0x13
CMD_STATUS = 0xF0

ST_BOOT, ST_AUTH, ST_READY, ST_CAPTURE, ST_SEND, ST_AUTH_REJECTED = 0, 1, 2, 3, 4, 5

STATE_NAMES = {
    0: "BOOT", 1: "AUTH", 2: "READY", 3: "CAPTURE", 4: "SEND", 5: "AUTH REJECTED",
    225: "CAMERA INIT ERROR", 226: "TCP CONNECT ERROR", 227: "WIFI ERROR",
    228: "ZOOM ERROR", 231: "CAPTURE ERROR", 232: "BUFFER ERROR",
    233: "TCP WRITE ERROR", 234: "EXPOSURE ERROR", 235: "NETWORK CONFIG ERROR",
}

# Geometría canónica del mosaico; la columna X=640 tiene prioridad de refuerzo.
def positions():
    return [(x, y) for y in GRID_Y for x in GRID_X]


PRIORITY_TILE_IDS_DEFAULT = tuple(
    tile_id for tile_id, (x, _y) in enumerate(positions()) if x == 640
)


class I2CResponseError(RuntimeError):
    """El último frame de estado por I2C no es confiable."""


def log(message):
    print(message, flush=True)


# ============================================================
# Canal de control (I2C)
# ============================================================

def poll_response(bridge):
    if not bridge.call("vision_poll_response"):
        raise I2CResponseError("I2C response missing or invalid (see UNO Q/Nicla serial)")


def read_status(bridge):
    poll_response(bridge)
    state = int(bridge.call("vision_status"))
    if state in (253, 255):
        raise I2CResponseError("I2C response is not a valid STATUS frame")
    if state == ST_AUTH_REJECTED:
        raise RuntimeError("AUTH rejected by Nicla")
    if state >= 225:
        raise RuntimeError(STATE_NAMES.get(state, f"NICLA ERROR {state}"))
    return state


def wait_status(bridge, expected, timeout):
    deadline = time.monotonic() + timeout
    last_state, failures = None, 0
    while time.monotonic() < deadline:
        try:
            state = read_status(bridge)
        except I2CResponseError as error:
            failures += 1
            if failures == 1 or failures % 25 == 0:
                log(f"I2C | waiting for {STATE_NAMES.get(expected, expected)} | failures={failures} | {error}")
            time.sleep(0.2)
            continue
        if failures:
            log(f"I2C | recovered after {failures} failed polls")
            failures = 0
        if state != last_state:
            log(f"NICLA | {STATE_NAMES.get(state, state)}")
        last_state = state
        if state == expected:
            return
        time.sleep(0.2)
    raise TimeoutError(f"Timed out waiting for {STATE_NAMES.get(expected, expected)} "
                        f"after {timeout}s (last state={STATE_NAMES.get(last_state, last_state)})")


def authenticate(bridge, timeout=10.0):
    nonce = secrets.token_bytes(8)
    nonce_hi, nonce_lo = struct.unpack(">II", nonce)
    if not bridge.call("vision_auth_challenge", nonce_hi, nonce_lo):
        raise RuntimeError("AUTH_CHALLENGE could not be sent over I2C")
    deadline = time.monotonic() + timeout
    mac = None
    while time.monotonic() < deadline:
        try:
            poll_response(bridge)
        except I2CResponseError:
            time.sleep(0.05)
            continue
        if int(bridge.call("vision_response_command")) == CMD_AUTH_RESPONSE:
            words = [int(bridge.call("vision_auth_mac_word", i)) for i in range(4)]
            mac = b"".join(struct.pack(">I", word) for word in words)
            break
        time.sleep(0.05)
    if mac is None:
        raise TimeoutError("Timed out waiting for AUTH_RESPONSE")
    from secrets_auth import AUTH_SHARED_KEY
    key = AUTH_SHARED_KEY.encode() if isinstance(AUTH_SHARED_KEY,str) else AUTH_SHARED_KEY
    expected = hmac.new(key, nonce + AUTH_CONTEXT, hashlib.sha256).digest()[:16]
    ok = hmac.compare_digest(mac, expected)
    if not bridge.call("vision_auth_ack", ok):
        raise RuntimeError("AUTH_ACK could not be sent over I2C")
    if not ok:
        raise RuntimeError("AUTH rejected: Nicla does not hold the shared key")
    log("AUTH | accepted, waiting for camera/WiFi warmup")
    wait_status(bridge, ST_READY, 90)


def set_diagnostic_mode(bridge, enabled):
    if not bridge.call('vision_set_diagnostic', bool(enabled)):
        raise RuntimeError('SET_DIAGNOSTIC delivery failed')
    deadline=time.monotonic()+10
    while time.monotonic()<deadline:
        try:
            state=read_status(bridge)
            if (state==ST_READY and int(bridge.call('vision_diagnostic_enabled'))==int(enabled)
                    and int(bridge.call('vision_last_result'))==0):
                return
        except I2CResponseError:
            pass
        time.sleep(0.05)
    raise TimeoutError('Nicla did not confirm diagnostic mode')


# ============================================================
# Recepción TCP — igual patrón probado de versiones anteriores
# ============================================================

def receive_exact(conn, count, deadline, label=None, sink=None):
    data = bytearray()
    next_progress = time.monotonic() + 5
    while len(data) < count:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError(f"{label or 'tile'} timeout at {len(data)}/{count} bytes")
        if label and time.monotonic() >= next_progress:
            log(f"VISION | {label} | received {len(data)}/{count} bytes")
            next_progress = time.monotonic() + 5
        conn.settimeout(min(0.25, remaining))
        try:
            chunk = conn.recv(min(16384, count - len(data)))
        except socket.timeout:
            continue
        if not chunk:
            raise ConnectionError(f"tile closed at {len(data)}/{count} bytes")
        if sink is not None:
            sink.write(chunk)
        data.extend(chunk)
    return bytes(data)


def receive_line(conn, deadline, label=None):
    data = bytearray()
    while len(data) < 96:
        value = receive_exact(conn, 1, deadline, label)
        if value == b"\n":
            return data.decode("ascii").rstrip("\r")
        data.extend(value)
    raise ValueError("header line exceeds 95 bytes")


# ============================================================
# Contenido — decodificación RGB565 + cribado cromático en una sola pasada
# ============================================================
# Cribado cromático: rango de canal > 120 y canal máximo >= 192.
# Se evalúa durante la decodificación, sin otra pasada ni dependencia de NumPy.
# Es una señal de sospecha; no demuestra por sí sola corrupción.

def decode_and_screen(raw):
    if len(raw)!=TILE_BYTES:
        raise ValueError("RGB565 payload length mismatch")
    rgb = bytearray(TILE_W * TILE_H * 3)
    extreme = 0
    for index in range(TILE_W * TILE_H):
        value = (raw[2 * index] << 8) | raw[2 * index + 1]
        r = ((value >> 11) & 31) << 3
        g = ((value >> 5) & 63) << 2
        b = (value & 31) << 3
        rgb[3 * index] = r
        rgb[3 * index + 1] = g
        rgb[3 * index + 2] = b
        hi = r if r >= g else g
        hi = hi if hi >= b else b
        lo = r if r <= g else g
        lo = lo if lo <= b else b
        if hi - lo > 120 and hi >= 192:
            extreme += 1
    image = Image.frombytes("RGB", (TILE_W, TILE_H), bytes(rgb))
    return image, extreme



HEADER_FIELDS = (
    'width','height','size','capture_id','capture_seq','tile_id','x','y','request_ordinal',
    'reinforced','attempt','capture_completed_ms','capture_duration_ms','discard_duration_ms',
    'frame_duration_ms','diagnostic_enabled','crc32_source','wide_window','acquisition_flags','capture_result','frame_events','dma_remaining','dcmi_errors','dma_errors')


def receive_tile(conn, expected, raw_path, record, timeout=60):
    deadline=time.monotonic()+timeout
    record['stage']='header'
    if (receive_line(conn,deadline),receive_line(conn,deadline)) != ('SIXQ_TILE_V7','RGB565'):
        raise ValueError('Wrong firmware/packet: expected V7 wire 9')
    header={name:int(receive_line(conn,deadline)) for name in HEADER_FIELDS}
    if receive_line(conn,deadline)!='':
        raise ValueError('Missing header terminator')
    record['header']=header
    if any(not 0<=value<=0xffffffff for value in header.values()):
        raise ValueError('Header value out of range')
    if tuple(header[k] for k in ('width','height','size'))!=(TILE_W,TILE_H,TILE_BYTES):
        raise ValueError('Wrong dimensions/size')
    for key,value in expected.items():
        if header[key]!=value:
            raise ValueError('Header identity/mode mismatch: '+key)
    for key in ('reinforced','diagnostic_enabled','wide_window'):
        if header[key] not in (0,1):
            raise ValueError('Invalid flag: '+key)
    if header['wide_window'] != header['reinforced']:
        raise ValueError('Crop profile mismatch')
    record['stage']='payload'
    started=time.monotonic_ns()
    raw_path=Path(raw_path);partial=Path(str(raw_path)+'.partial')
    with partial.open('xb') as stream:
        raw=receive_exact(conn,TILE_BYTES,deadline,'payload',sink=stream)
    partial.replace(raw_path)
    record.update(raw_complete=True,received_bytes=len(raw),receive_duration_ms=(time.monotonic_ns()-started)/1e6)
    crc=zlib.crc32(raw)&0xffffffff
    valid=(header['acquisition_flags']==0 and header['capture_result']==0 and
           header['frame_events']==1 and
           header['dma_remaining']==0 and header['dcmi_errors']==0 and header['dma_errors']==0)
    record.update(crc32_source=header['crc32_source'],crc32_receiver=crc,
                  integrity_status='match' if crc==header['crc32_source'] else 'mismatch',
                  acquisition_valid=valid)
    return raw


def save_manifest(directory, manifest):
    temporary=directory/'manifest.tmp'
    temporary.write_text(json.dumps(manifest,indent=2,allow_nan=False)+'\n')
    temporary.replace(directory/'manifest.json')


def capture_tile(bridge, server, directory, capture_id, seq, tile, ordinal, settings, manifest):
    x,y=positions()[tile]
    reinforced=settings['reinforced_protocol_enabled'] and tile in settings['priority_tile_ids']
    selected=None
    for attempt in range(settings['max_retries_per_tile']+1):
        expected=dict(capture_id=capture_id,capture_seq=seq,tile_id=tile,x=x,y=y,
                      request_ordinal=ordinal,reinforced=int(reinforced),attempt=attempt,
                      diagnostic_enabled=int(settings['diagnostic_enabled']))
        stem=f'tile_{tile:02d}_attempt_{attempt:02d}_x{x:04d}_y{y:04d}'
        record=dict(expected,raw_file=stem+'.rgb565',file=stem+'.png',raw_complete=False,
                    image_saved=False,selected=False,accepted=False,stage='request',status='requested')
        manifest['attempts'].append(record);save_manifest(directory,manifest)
        image=None
        try:
            log(f'V7 | TILE={tile} ATTEMPT={attempt} REINFORCED={int(reinforced)}')
            if not bridge.call('vision_send_tile',capture_id,seq,tile,x,y,ordinal,int(reinforced),attempt):
                raise RuntimeError('SEND_TILE delivery failed')
            record['stage']='accept';server.settimeout(60)
            conn,peer=server.accept()
            try:
                record['peer']=peer[0]
                raw=receive_tile(conn,expected,directory/record['raw_file'],record)
            finally:
                conn.close()
            save_manifest(directory,manifest)
            record['stage']='content'
            image,count=decode_and_screen(raw)
            record.update(extreme_chroma_pixels=count,anomaly=count>settings['anomaly_threshold'],
                          sha256=hashlib.sha256(raw).hexdigest())
            record['stage']='png';image.save(directory/record['file']);record['image_saved']=True
            record['stage']='ready';wait_status(bridge,ST_READY,30)
            record['ready_confirmed']=True
            record['technical_valid']=record['acquisition_valid'] and record['integrity_status']=='match'
            record['accepted']=record['technical_valid'] and not record['anomaly']
            record['status']='accepted' if record['accepted'] else 'suspect'
            # Prefer technically valid evidence if no attempt can be accepted.
            if selected is None or record['accepted'] or (record['technical_valid'] and not selected['technical_valid']):
                selected=record
            save_manifest(directory,manifest)
            if record['accepted']:
                break
            if not settings['reinforced_protocol_enabled']:
                break
            reinforced=True
        except Exception as error:
            record.update(status='error',error=str(error))
            partial=directory/(record['raw_file']+'.partial')
            if partial.exists():record['partial_bytes']=partial.stat().st_size
            save_manifest(directory,manifest)
            raise
        finally:
            if image is not None:image.close()
    selected['selected']=True
    selected['retries_exhausted']=not selected['accepted']
    return selected


def assemble(directory, records):
    mosaic=Image.new('RGB',(FULL_W,FULL_H))
    try:
        for record in sorted(records,key=lambda r:r['tile_id']):
            if record.get('image_saved'):
                with Image.open(directory/record['file']) as tile:
                    mosaic.paste(tile,(record['x'],record['y']))
        return mosaic
    except Exception:
        mosaic.close();raise
