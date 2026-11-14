#!/usr/bin/env python3
"""Run once as root after installing Mosquitto; never print credentials."""
import os
from pathlib import Path
import pwd
import secrets
import shutil
import subprocess

if os.geteuid() != 0:
    raise SystemExit("Run as root")
try:
    account = pwd.getpwnam("roocore")
except KeyError:
    subprocess.run(["useradd", "--system", "--home-dir", "/var/lib/roocore",
                    "--shell", "/usr/sbin/nologin", "roocore"], check=True)
    account = pwd.getpwnam("roocore")

secret_dir = Path("/etc/roocore/secrets")
access_dir = Path("/root/roocore-access")
for folder in (secret_dir, access_dir):
    folder.mkdir(parents=True, exist_ok=True, mode=0o700)
    folder.chmod(0o700)
os.chown(secret_dir, account.pw_uid, account.pw_gid)

def get_secret(path, owner=None):
    if not path.exists():
        fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, "w") as f:
            f.write(secrets.token_urlsafe(36) + "\n")
    path.chmod(0o600)
    if owner:
        os.chown(path, owner.pw_uid, owner.pw_gid)
    return path.read_text().strip()

admin = get_secret(secret_dir / "admin_password", account)
reader = get_secret(secret_dir / "mqtt_reader_password", account)
get_secret(secret_dir / 'auth_secret', account)
robot = get_secret(access_dir / "roomba-620_password")
sim = get_secret(access_dir / "sim-620_password")

passwords = Path("/etc/mosquitto/roocore-passwd")
temporary = passwords.with_name(".roocore-passwd.new")
fd = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
with os.fdopen(fd, "w") as f:
    for user, password in (("api-reader", reader), ("roomba-620", robot), ("sim-620", sim)):
        f.write(user + ":" + password + "\n")
subprocess.run(["mosquitto_passwd", "-U", str(temporary)], check=True)
mqtt_account = pwd.getpwnam("mosquitto")
os.chown(temporary, mqtt_account.pw_uid, mqtt_account.pw_gid)
temporary.chmod(0o600)
os.replace(temporary, passwords)

base = Path(__file__).resolve().parent.parent
shutil.copyfile(base / "mosquitto/acl", "/etc/mosquitto/roocore-acl")
shutil.copyfile(base / "mosquitto/mosquitto.conf", "/etc/mosquitto/conf.d/roocore.conf")
Path("/etc/mosquitto/conf.d/roocore.conf").chmod(0o644)
Path("/etc/mosquitto/roocore-acl").chmod(0o600)
os.chown("/etc/mosquitto/roocore-acl", mqtt_account.pw_uid, mqtt_account.pw_gid)
print("Provisioned private MQTT identities and administrator credentials; values were not printed.")
