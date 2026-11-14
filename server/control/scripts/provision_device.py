"""Generate/rotate one device-only credential. Run as root, never put its output in Git."""
import argparse
import hashlib
import os
from pathlib import Path
import secrets
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from app.config import Settings
from app.store import Store, ROBOTS
from app.device_stream import DeviceKey
from sqlalchemy import select


def main():
    parser=argparse.ArgumentParser();parser.add_argument('robot',choices=ROBOTS);parser.add_argument('--rotate',action='store_true')
    args=parser.parse_args()
    if os.geteuid()!=0:raise SystemExit('Run on the server as root')
    os.umask(0o077)
    folder=Path('/root/roocore-access');folder.mkdir(mode=0o700,exist_ok=True)
    path=folder/(args.robot+'-https-token')
    store=Store(Settings.from_env().database_url)
    with store.session.begin() as s:
        key=s.get(DeviceKey,args.robot)
        if key and not args.rotate:
            print('Credential already exists; no change');return
        token=secrets.token_urlsafe(48)
        if not key:key=DeviceKey(robot_id=args.robot);s.add(key)
        key.token_hash=hashlib.sha256(token.encode()).hexdigest();key.enabled=True
        temp=path.with_suffix('.new')
        with temp.open('x') as handle:handle.write(token+'\n');handle.flush();os.fsync(handle.fileno())
        os.replace(temp,path)
    print('Device credential saved privately:',path)

if __name__=='__main__':main()
