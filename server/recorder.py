"""RooCore TCP recorder. The ESP connects here over the local Wi-Fi network."""
import argparse
import csv
import hmac
import json
import math
import re
import socket
import sys
import time
from pathlib import Path
from datetime import datetime, timezone

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from oi import CaptureDecoder, FIELDS

MAX_LINE = 2048

class Run:
    def __init__(self, directory, profile):
        self.directory = Path(directory)
        self.directory.mkdir(parents=True,exist_ok=False)
        self.raw = (self.directory/'transport.jsonl').open('w')
        self.events = (self.directory/'events.jsonl').open('w')
        self.csv = (self.directory/'sensors.csv').open('w',newline='')
        self.writer = csv.DictWriter(self.csv,fieldnames=[
            'received_utc','boot','capture','device_ms','seq','segment','raw_frame',
            *[f[0] for f in FIELDS.values()]])
        self.writer.writeheader()
        self.decoder = CaptureDecoder()
        self.metadata = dict(protocol=2,profile=profile,started_utc=time.time(),
                             frames=0,connections=0,device=None,hardware_verified=False)
        self.event('run_started',profile=profile)

    def event(self, kind, **data):
        self.events.write(json.dumps(dict(received_utc=time.time(),event=kind,**data))+'\n')

    def receive(self, record):
        # Whitelist fields so credentials/unknown peer fields never enter recordings.
        keys = ['type','boot','capture','ms','overruns','dropped']
        keys += ['seq','hex'] if record.get('type')=='serial' else ['recording']
        clean = {k:record[k] for k in keys if k in record}
        rows = self.decoder.feed(clean)
        received = time.time()
        self.raw.write(json.dumps(dict(received_utc=received,**clean))+'\n')
        for row in rows:
            self.writer.writerow(dict(received_utc=received,**row))
            self.metadata['frames'] += 1

    def flush(self):
        for f in (self.raw,self.events,self.csv): f.flush()

    def close(self):
        self.event('run_stopped',frames=self.metadata['frames'])
        self.metadata.update(ended_utc=time.time(),
            missing_serial_records=self.decoder.missing_records,decode_errors=self.decoder.stream.errors)
        (self.directory/'session.json').write_text(json.dumps(self.metadata,indent=2)+'\n')
        for f in (self.raw,self.events,self.csv): f.close()


def serve_connection(conn, peer, run, token, should_stop):
    """One authenticated recorder link; reconnects stay in the same run directory."""
    conn.settimeout(0.2)
    buffer = bytearray()
    authenticated = False
    started = last_received = time.monotonic()
    last_ping = last_flush = last_report = started
    reason = 'server_stop'
    try:
        while not should_stop():
            now = time.monotonic()
            if now-last_received > (6 if authenticated else 5):
                reason = 'heartbeat_timeout' if authenticated else 'hello_timeout'
                break
            if authenticated and now-last_ping >= 2:
                conn.sendall(b'PING\n'); last_ping=now
            try:
                chunk = conn.recv(4096)
            except socket.timeout:
                chunk = None
            if chunk == b'':
                reason = 'peer_closed'; break
            if chunk:
                buffer.extend(chunk)
                while b'\n' in buffer:
                    pos=buffer.index(10)
                    if pos>MAX_LINE: raise ValueError('Record too long')
                    line=bytes(buffer[:pos]); del buffer[:pos+1]
                    record=json.loads(line)
                    if not isinstance(record,dict): raise ValueError('Record must be an object')
                    if not authenticated:
                        supplied=record.get('token','')
                        device=record.get('device','')
                        if (record.get('type')!='hello' or record.get('protocol')!=2 or
                                not isinstance(supplied,str) or not supplied.isascii() or
                                not hmac.compare_digest(supplied,token) or
                                not isinstance(device,str) or not re.fullmatch(r'roocore-[0-9a-f]{6,8}',device)):
                            reason='authentication_failed'; return
                        if run.metadata['device'] not in (None,device):
                            reason='different_device'; return
                        run.decoder.uint(record,'boot')
                        authenticated=True
                        run.metadata['device']=device
                        run.metadata['connections'] += 1
                        run.decoder.disconnect()
                        run.event('connected',device=device,peer=peer[0],boot=record['boot'])
                        conn.sendall(('READY\nSTART '+run.metadata['profile']+'\n').encode())
                        run.event('start_requested',profile=run.metadata['profile'])
                    else:
                        run.receive(record)
                    last_received=time.monotonic()
                if len(buffer)>MAX_LINE: raise ValueError('Record too long')
            if now-last_flush >= 1:
                run.flush(); last_flush=now
            if authenticated and now-last_report >= 5:
                print(f"{run.metadata['frames']} frames; {run.decoder.missing_records} missing records; "
                      f"{run.decoder.stream.errors} decode errors",flush=True)
                last_report=now
    except (ValueError,UnicodeError,OSError) as exc:
        # Log the error category, never untrusted payloads or the token.
        reason=type(exc).__name__
    finally:
        if authenticated:
            try: conn.sendall(b'STOP\n')
            except OSError: pass
            run.event('disconnected',reason=reason,partial_record_bytes=len(buffer))
            run.decoder.disconnect()
        else:
            run.event('connection_rejected',reason=reason,peer=peer[0])
        run.flush()
        conn.close()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bind',default='0.0.0.0')
    parser.add_argument('--port',type=int,default=8765)
    parser.add_argument('--token-file',type=Path,default=Path('.roocore-token'))
    parser.add_argument('--profile',choices=['basic','wheels'],default='wheels')
    parser.add_argument('--seconds',type=float,help='Optional limit; otherwise record until Ctrl-C')
    parser.add_argument('--output',type=Path,default=Path('recordings'))
    args=parser.parse_args()
    if not 1<=args.port<=65535: parser.error('Port must be 1–65535')
    if args.seconds is not None and (not math.isfinite(args.seconds) or args.seconds<=0):
        parser.error('--seconds must be positive and finite')
    try: token=args.token_file.read_text().strip()
    except OSError as exc: parser.error(f'Cannot read token file: {exc}')
    if not re.fullmatch('[0-9a-f]{48}',token): parser.error('Expected the 48-character token from tools/configure.py')
    path=args.output/datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S-%fZ')
    begin=time.monotonic()
    def stop(): return args.seconds is not None and time.monotonic()-begin>=args.seconds
    with socket.socket(socket.AF_INET,socket.SOCK_STREAM) as listener:
        listener.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
        listener.bind((args.bind,args.port)); listener.listen(1); listener.settimeout(0.5)
        run=Run(path,args.profile)
        print(f'Listening on {args.bind}:{args.port}. Recording: {path}',flush=True)
        print('Press CLEAN on the robot. Ctrl-C ends recording, not cleaning.',flush=True)
        try:
            while not stop():
                try: conn,peer=listener.accept()
                except socket.timeout: continue
                serve_connection(conn,peer,run,token,stop)
        except KeyboardInterrupt:
            pass
        finally:
            run.close()
            print(f"Saved {run.metadata['frames']} frames in {path}")

if __name__=='__main__': main()
