"""Write local ESP network settings for the RooCore server."""
import argparse
import getpass
import ipaddress
import json
import secrets
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--ssid', required=True)
p.add_argument('--server', required=True, help='IPv4 address of our server on the same LAN')
p.add_argument('--port', type=int, default=8765)
a = p.parse_args()
ipaddress.IPv4Address(a.server)
if not 1 <= a.port <= 65535: p.error('Port must be 1–65535')
root = Path(__file__).resolve().parents[1]
config = root/'firmware/include/network_config.h'
tokenfile = root/'.roocore-token'
if config.exists() or tokenfile.exists():
    raise SystemExit('Local configuration exists; edit it explicitly instead of overwriting it.')
password = getpass.getpass('Wi-Fi password: ')
token = secrets.token_hex(24)
values = dict(WIFI_SSID=a.ssid, WIFI_PASSWORD=password, SERVER_IP=a.server,
              SERVER_PORT=a.port, DEVICE_TOKEN=token)
with config.open('x') as file:
    file.write('#pragma once\n' + ''.join(f'#define {k} {json.dumps(v, ensure_ascii=True)}\n' for k,v in values.items()))
config.chmod(0o600)
with tokenfile.open('x') as file: file.write(token+'\n')
tokenfile.chmod(0o600)
print('Saved firmware/include/network_config.h and .roocore-token. No device was flashed.')
