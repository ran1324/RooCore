"""Native-cleaning OI capture and qualified firmware-only OTA for RooCore XIAO."""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import time
import urllib.parse
import urllib.request
import urllib.error

ROOT = Path(__file__).resolve().parents[1]
SENSORS = json.loads((ROOT / 'docs/oi-map.json').read_text())['sensors']
FIELDS = {str(s['id']): s['name'] for s in SENSORS}
TARGET = 'esp32:esp32:XIAO_ESP32S3:PSRAM=opi,FlashSize=8M,PartitionScheme=default_8MB'


class Client:
    def __init__(self, base, token):
        parsed = urllib.parse.urlsplit(base)
        if parsed.scheme != 'http' or not parsed.hostname or parsed.username or parsed.password:
            raise ValueError('Use http://XIAO-IP on a trusted LAN without credentials in the URL')
        self.base, self.token = base.rstrip('/'), token

    def call(self, path, data=None):
        body = urllib.parse.urlencode(data).encode() if data is not None else None
        req = urllib.request.Request(self.base + path, data=body,
            headers={'Authorization': 'Bearer ' + self.token})
        # Redirects must not forward the token to another host.
        class NoRedirect(urllib.request.HTTPRedirectHandler):
            def redirect_request(self, *args):
                return None
        with urllib.request.build_opener(NoRedirect).open(req, timeout=5) as response:
            raw = response.read(1024 * 1024)
            if response.headers.get_content_type() == 'application/json':
                return json.loads(raw)
            return raw.decode()

    def command(self, action, **args):
        return self.call('/command', dict(action=action, **args))

    def recording(self, after=0, identity=None):
        args = dict(after=after)
        if identity:
            args.update(boot_id=identity[0], session=identity[1])
        return self.call('/recording?' + urllib.parse.urlencode(args))


class Energy:
    """Integrate only contiguous valid samples; never fill network/source gaps."""
    def __init__(self):
        self.previous = None
        self.discharge_wh = self.charge_wh = self.observed_s = 0.0
        self.intervals_skipped = 0

    def add(self, row):
        values = row['sensors']
        mv, ma = values.get('22'), values.get('23')
        power = None
        if type(mv) is int and 0 < mv <= 65535 and type(ma) is int and -32768 <= ma <= 32767:
            power = mv * ma / 1_000_000.0 # positive into battery
        previous, self.previous = self.previous, (row, power)
        if previous is not None:
            old, old_power = previous
            dt = (row['ms'] - old['ms']) & 0xffffffff
            contiguous = (row['sequence'] == old['sequence'] + 1 and
                row['rejected_frames'] == old['rejected_frames'] and
                row['source_lost'] == old['source_lost'] and 0 < dt <= 200)
            if contiguous and power is not None and old_power is not None:
                hours = dt / 3_600_000.0
                # Linear interpolation, split the interval at any current sign crossing.
                if old_power * power < 0:
                    fraction = abs(old_power) / (abs(old_power) + abs(power))
                    self.discharge_wh += (max(-old_power, 0) * fraction + max(-power, 0) * (1-fraction)) * hours / 2
                    self.charge_wh += (max(old_power, 0) * fraction + max(power, 0) * (1-fraction)) * hours / 2
                else:
                    self.discharge_wh += (max(-old_power, 0) + max(-power, 0)) * hours / 2
                    self.charge_wh += (max(old_power, 0) + max(power, 0)) * hours / 2
                self.observed_s += dt / 1000.0
            else:
                self.intervals_skipped += 1
        return None if power is None else -power

    def summary(self):
        return dict(discharge_wh=self.discharge_wh, charge_wh=self.charge_wh,
            integrated_seconds=self.observed_s, skipped_intervals=self.intervals_skipped,
            method='trapezoidal, <=200ms contiguous samples, sign crossings split',
            boundary='Roomba-reported battery shunt; external branch may bypass it',
            hardware_verified=False)


class CaptureWriter:
    def __init__(self, directory):
        directory.mkdir(parents=True, exist_ok=False)
        self.directory = directory
        self.raw = (directory / 'frames.jsonl').open('w')
        self.csv = (directory / 'sensors.csv').open('w', newline='')
        self.columns = ['sequence', 'ms', 'generation', 'rejected_frames', 'source_lost',
                        'received_utc', 'battery_discharge_w', *FIELDS.values()]
        self.writer = csv.DictWriter(self.csv, fieldnames=self.columns)
        self.writer.writeheader()
        self.started_utc = time.time()
        self.energy = Energy()
        self.identity = None
        self.after = self.frames = self.lost = 0
        self.meta = None

    def accept(self, batch):
        identity = batch['boot_id'], batch['session']
        if self.identity is not None and identity != self.identity:
            raise ValueError('Robot reboot/session changed; refusing to merge recordings')
        if batch['schema'] != 1:
            raise ValueError('Unsupported recording schema')
        self.identity, self.meta = identity, {k: v for k, v in batch.items() if k != 'records'}
        rows = batch['records']
        received_utc = time.time()
        for frame in rows:
            if frame['sequence'] <= self.after:
                raise ValueError('Duplicate/backwards recording sequence')
            self.lost += frame['sequence'] - self.after - 1
            power = self.energy.add(frame)
            self.raw.write(json.dumps(dict(received_utc=received_utc, **frame), separators=(',', ':')) + '\n')
            fields = {name: frame['sensors'].get(packet) for packet, name in FIELDS.items()}
            self.writer.writerow(dict(**{k: frame[k] for k in self.columns[:5]},
                                      received_utc=received_utc, battery_discharge_w=power, **fields))
            self.after = frame['sequence']
            self.frames += 1
        if batch['next'] != self.after:
            raise ValueError('Invalid next cursor')
        self.raw.flush()
        self.csv.flush()

    def close(self, result):
        self.raw.close()
        self.csv.close()
        (self.directory / 'session.json').write_text(json.dumps(dict(
            started_utc=self.started_utc, ended_utc=time.time(),
            result=result, frames=self.frames, lost_records=self.lost,
            device=self.meta, energy=self.energy.summary(),
            sensors=SENSORS, unavailable='blank CSV fields; omitted raw sensor IDs',
            native_motor_pwm='not reported by OI; no vacuum-current/RPM/pressure packet'), indent=2) + '\n')


def wait_for(client, predicate, timeout=10):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        try:
            value = client.call('/status')
        except (urllib.error.URLError, TimeoutError, ConnectionError):
            time.sleep(.2)
            continue
        if predicate(value):
            return value
        time.sleep(.2)
    raise RuntimeError('Command not admitted/completed; inspect /status last error')


def record(client, profile, directory, seconds, attach=False):
    before = client.recording()
    if before['recording'] and not attach:
        raise RuntimeError('An existing recording is active; use fetch or stop explicitly')
    if attach and not before['session']:
        raise RuntimeError('No existing recording session to fetch')
    writer = CaptureWriter(directory)
    result = 'failed'
    started = False
    try:
        if not attach:
            client.command('record-start', profile=profile)
        deadline = time.monotonic() + 5
        while True:
            batch = client.recording()
            if batch['boot_id'] != before['boot_id']:
                raise RuntimeError('Robot rebooted during recording setup')
            if attach or (batch['session'] != before['session'] and batch['recording']):
                started = True
                writer.accept(batch)
                break
            if time.monotonic() > deadline:
                raise RuntimeError('record-start rejected or stream failed; inspect /status')
            time.sleep(.1)
        print('Recording. Press CLEAN on the robot for native cleaning. Ctrl-C stops recording only.', flush=True)
        until = None if seconds is None else time.monotonic() + seconds
        try:
            while batch['recording'] and (until is None or time.monotonic() < until):
                batch = client.recording(writer.after, writer.identity)
                writer.accept(batch)
                if not batch['recording']:
                    break
                if not batch['has_more']:
                    time.sleep(.1)
        except KeyboardInterrupt:
            pass
        result = 'complete'
    finally:
        if started:
            try:
                client.command('record-stop')
                deadline = time.monotonic() + 5
                while True:
                    batch = client.recording(writer.after, writer.identity)
                    writer.accept(batch)
                    if not batch['recording'] and not batch['has_more']:
                        break
                    if time.monotonic() > deadline:
                        raise RuntimeError('record-stop/drain timeout')
                    if not batch['has_more']:
                        time.sleep(.1)
            except Exception:
                result = 'interrupted_stop_or_drain_failed'
                raise
            finally:
                writer.close(result)
        else:
            try:
                client.command('record-stop')
            finally:
                writer.close(result)
    print(f'Saved {writer.frames} frames to {directory}; lost {writer.lost}. Native observation hold remains.')


def firmware_manifest(binary):
    data = binary.read_bytes()
    # ESP-IDF image header esp_image_header_t: magic, target chip ID at byte 12.
    if len(data) < 24 or data[0] != 0xe9 or int.from_bytes(data[12:14], 'little') != 9:
        raise ValueError('Expected an ESP32-S3 application image, not merged bootloader/filesystem')
    if len(data) > 0x330000:
        raise ValueError('Image exceeds default_8MB OTA app slot')
    return dict(schema=1, target=TARGET, binary=binary.name,
        sha256=hashlib.sha256(data).hexdigest(), bytes=len(data), chip='ESP32-S3')


def ota(client, manifest_path, password_file, espota):
    manifest = json.loads(manifest_path.read_text())
    binary = manifest_path.parent / manifest['binary']
    if manifest != firmware_manifest(binary):
        raise ValueError('Firmware manifest/target/digest does not match the application image')
    password = password_file.read_text().strip()
    if len(password) < 24:
        raise ValueError('OTA password must be at least 24 characters')
    host = urllib.parse.urlsplit(client.base).hostname
    status = client.call('/status')
    boot = status['boot_id']
    client.command('ota-arm', confirm='stop-native-and-hold')
    wait_for(client, lambda s: s['boot_id'] == boot and s['ota_ready'], timeout=15)
    # Use the official uploader in-process; the password never enters OS command arguments.
    spec = importlib.util.spec_from_file_location('roocore_espota', espota)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    rc = module.main(['--ip', host, '--port', '3232', '--auth', password, '--file', str(binary)])
    if rc:
        raise RuntimeError('OTA upload failed; service hold retained, recover via USB if needed')
    status = wait_for(client, lambda s: s['boot_id'] != boot, timeout=30)
    print('New boot observed:', status['boot_id'], status.get('firmware_version'), status.get('firmware_build'))
    print('This confirms a reboot, not physical hardware validation. Reanchor before navigation.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', help='http://XIAO-IP on trusted LAN')
    parser.add_argument('--token-file', type=Path, default=Path('.roocore-token'))
    sub = parser.add_subparsers(dest='operation', required=True)
    rec = sub.add_parser('record')
    rec.add_argument('--profile', choices=['base', 'extended'], default='base')
    rec.add_argument('--output', type=Path, required=True)
    rec.add_argument('--seconds', type=float)
    fetch = sub.add_parser('fetch')
    fetch.add_argument('--output', type=Path, required=True)
    fetch.add_argument('--seconds', type=float)
    for name in ('status', 'stop', 'exit', 'ota-exit'):
        sub.add_parser(name)
    update = sub.add_parser('ota')
    update.add_argument('--manifest', type=Path, required=True)
    update.add_argument('--password-file', type=Path, required=True)
    update.add_argument('--espota', type=Path, required=True, help='tools/espota.py in installed Arduino ESP32 core')
    manifest = sub.add_parser('manifest')
    manifest.add_argument('binary', type=Path)
    args = parser.parse_args()
    if args.operation == 'manifest':
        print(json.dumps(firmware_manifest(args.binary), indent=2))
        return
    if not args.url:
        parser.error('--url is required')
    if args.operation in ('record', 'fetch') and args.seconds is not None and (not math.isfinite(args.seconds) or args.seconds <= 0):
        parser.error('--seconds must be finite and positive')
    client = Client(args.url, args.token_file.read_text().strip())
    if args.operation == 'record':
        record(client, args.profile, args.output, args.seconds)
    elif args.operation == 'fetch':
        record(client, None, args.output, args.seconds, attach=True)
    elif args.operation == 'status':
        print(json.dumps(client.call('/status'), indent=2))
        print(json.dumps(client.recording(), indent=2))
    elif args.operation == 'stop':
        print(client.command('record-stop'))
    elif args.operation == 'exit':
        print(client.command('record-exit', confirm='stop-native-and-hold'))
    elif args.operation == 'ota-exit':
        print(client.command('ota-exit'))
    elif args.operation == 'ota':
        ota(client, args.manifest, args.password_file, args.espota)


if __name__ == '__main__':
    main()
