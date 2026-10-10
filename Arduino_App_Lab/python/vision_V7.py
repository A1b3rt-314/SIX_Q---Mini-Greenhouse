# SPDX-License-Identifier: 0BSD
"""V7 fixed-sensor coordinator. Commands: status, diagnostic, reinforced, order,
retries, threshold, priority, capture. Changes take effect next mosaic only.
"""
import copy
import fcntl
import json
import os
import secrets
import socket
import tempfile
from datetime import datetime, timezone
from pathlib import Path
import vision_capture_V7 as capture

HOST, PORT = '0.0.0.0', 5000
OUTPUT_DIR = Path('/app/captures')
CONFIG_PATH = Path(os.environ.get('SIXQ_V7_CONFIG','/app/data/vision_v7_runtime.json'))
# Share exclusion with the current V6 when both are configured with defaults.
CAPTURE_LOCK = Path(os.environ.get('SIXQ_VISION_CAPTURE_LOCK',
    os.environ.get('SIXQ_V6_CONFIG','/app/data/vision_v6_runtime.json')+'.capture.lock'))
DEFAULT_SETTINGS = dict(diagnostic_enabled=True,reinforced_protocol_enabled=True,
    tile_order_mode='normal',priority_tile_ids=[2,7,12,17],anomaly_threshold=100,max_retries_per_tile=2)
PERMUTED_TILE_ORDER = (0,3,8,13,18,1,4,9,14,19,15,5,10,6,16,2,11,7,17,12)
log=capture.log


def _validate_settings(data):
    if not isinstance(data,dict) or set(data)-set(DEFAULT_SETTINGS):
        raise ValueError('Configuration must be an object with known V7 settings')
    settings=copy.deepcopy(DEFAULT_SETTINGS);settings.update(data)
    for key in ('diagnostic_enabled','reinforced_protocol_enabled'):
        if type(settings[key]) is not bool:raise ValueError(key+' must be boolean')
    if settings['tile_order_mode'] not in ('normal','permuted'):
        raise ValueError('Invalid tile_order_mode')
    ids=settings['priority_tile_ids']
    if not isinstance(ids,list) or any(type(n) is not int or not 0<=n<20 for n in ids) or len(set(ids))!=len(ids):
        raise ValueError('priority_tile_ids must contain unique integers 0..19')
    for key,lo,hi in [('anomaly_threshold',0,102400),('max_retries_per_tile',0,5)]:
        if type(settings[key]) is not int or not lo<=settings[key]<=hi:
            raise ValueError(f'{key} must be integer {lo}..{hi}')
    return settings


def load_settings():
    try:return _validate_settings(json.loads(CONFIG_PATH.read_text()))
    except FileNotFoundError:return copy.deepcopy(DEFAULT_SETTINGS)


def save_settings(changes):
    CONFIG_PATH.parent.mkdir(parents=True,exist_ok=True)
    with Path(str(CONFIG_PATH)+'.lock').open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        settings=load_settings();settings.update(changes);settings=_validate_settings(settings)
        temporary=None
        try:
            with tempfile.NamedTemporaryFile(mode='w',dir=CONFIG_PATH.parent,delete=False) as stream:
                temporary=Path(stream.name)
                json.dump(settings,stream,indent=2,allow_nan=False)
                stream.flush();os.fsync(stream.fileno())
            temporary.replace(CONFIG_PATH)
        finally:
            if temporary:temporary.unlink(missing_ok=True)
    return settings


def request_order_for(mode):
    if mode not in ('normal','permuted'):raise ValueError('Invalid order')
    return list(PERMUTED_TILE_ORDER) if mode=='permuted' else list(range(20))


def capture_mosaic(bridge,output_dir=OUTPUT_DIR):
    settings=load_settings()
    CAPTURE_LOCK.parent.mkdir(parents=True,exist_ok=True)
    with CAPTURE_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX|fcntl.LOCK_NB)
        return _capture_locked(bridge,Path(output_dir),settings)


def _capture_locked(bridge,output_dir,settings):
    capture_id=secrets.randbelow(0x7fffffff)+1;seq=0
    start=datetime.now(timezone.utc)
    directory=output_dir/f'sixqv7_{start:%Y%m%dT%H%M%S_%fZ}_{capture_id}'
    directory.mkdir(parents=True,exist_ok=False)
    order=request_order_for(settings['tile_order_mode'])
    manifest=dict(variant='SIX_Q V7 fixed sensor',protocol=9,capture_id=capture_id,capture_seq=seq,
        width=1600,height=1200,tile_count=20,settings=settings,request_order=order,
        started_utc=start.isoformat(),complete=False,usable=False,quality_status='incomplete',
        integrity_status='unavailable',received_tiles=[],attempts=[],missing_tiles=list(range(20)),error=None)
    stage='listen';capture.save_manifest(directory,manifest)
    try:
        with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as server:
            server.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
            server.bind((HOST,PORT));server.listen(2)
            stage='authenticate';capture.authenticate(bridge)
            stage='diagnostic_mode';capture.set_diagnostic_mode(bridge,settings['diagnostic_enabled'])
            for ordinal,tile in enumerate(order):
                stage='tile';manifest['active_tile']=tile
                record=capture.capture_tile(bridge,server,directory,capture_id,seq,tile,ordinal,settings,manifest)
                manifest['received_tiles'].append(record);manifest['missing_tiles'].remove(tile)
                capture.save_manifest(directory,manifest)
        stage='assemble'
        with capture.assemble(directory,manifest['received_tiles']) as mosaic:
            mosaic.save(directory/'mosaico.png')
        selected=manifest['received_tiles']
        quality=('acquisition_or_integrity_failed' if any(not r['technical_valid'] for r in selected)
                 else 'suspect' if any(not r['accepted'] for r in selected) else 'screen_pass')
        manifest.update(complete=True,mosaic='mosaico.png',quality_status=quality,usable=quality=='screen_pass',
            integrity_status='match' if all(r['integrity_status']=='match' for r in selected) else 'mismatch',
            unresolved_tiles=[r['tile_id'] for r in selected if not r['accepted']],
            recovered_tiles=[r['tile_id'] for r in selected if r['accepted'] and r['attempt']>0])
    except Exception as error:
        manifest.update(error=str(error),error_stage=stage)
        if manifest['received_tiles']:
            try:
                with capture.assemble(directory,manifest['received_tiles']) as mosaic:
                    mosaic.save(directory/'mosaico_partial.png')
            except Exception as partial_error:manifest['partial_mosaic_error']=str(partial_error)
    finally:
        manifest['finished_utc']=datetime.now(timezone.utc).isoformat()
        capture.save_manifest(directory,manifest)
    return manifest


def _print_status():
    settings = load_settings()
    print(json.dumps(settings, indent=2))


def _cli(argv):
    if len(argv) < 2:
        print(__doc__)
        return 1
    command = argv[1]

    if command == "status":
        _print_status()
        return 0

    if command == "diagnostic" and len(argv) == 3 and argv[2] in ("on", "off"):
        save_settings({"diagnostic_enabled": argv[2] == "on"})
        _print_status()
        return 0

    if command == "reinforced" and len(argv) == 3 and argv[2] in ("on", "off"):
        save_settings({"reinforced_protocol_enabled": argv[2] == "on"})
        _print_status()
        return 0

    if command == "order" and len(argv) == 3 and argv[2] in ("normal", "permuted"):
        save_settings({"tile_order_mode": argv[2]})
        _print_status()
        return 0

    if command == "retries" and len(argv) == 3 and argv[2].isdigit():
        save_settings({"max_retries_per_tile": int(argv[2])})
        _print_status()
        return 0

    if command == "threshold" and len(argv) == 3 and argv[2].isdigit():
        save_settings({"anomaly_threshold": int(argv[2])})
        _print_status()
        return 0

    if command == "priority" and len(argv) == 3:
        try:
            ids = [int(part) for part in argv[2].split(",") if part.strip() != ""]
        except ValueError:
            print("priority requiere una lista de enteros separados por coma, ej: 2,7,12,17")
            return 1
        save_settings({"priority_tile_ids": ids})
        _print_status()
        return 0

    print(__doc__)
    return 1



def main():
    import sys
    if sys.argv[1:]==['capture']:
        from arduino.app_utils import Bridge
        manifest=capture_mosaic(Bridge)
        print(json.dumps(manifest,indent=2))
        return 0 if manifest['usable'] else 2
    return _cli(sys.argv)


if __name__=='__main__':
    raise SystemExit(main())
