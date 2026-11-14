"""Root-only recovery of the built-in owner account; no web reset bypass."""
import hashlib
import os
from pathlib import Path
import secrets
import sqlite3
import subprocess


def main():
    if os.geteuid() != 0:
        raise SystemExit('Run as root on the RooCore host')
    password = secrets.token_urlsafe(36)
    salt = secrets.token_hex(16)
    hashed = salt + ':' + hashlib.scrypt(password.encode(), salt=bytes.fromhex(salt), n=131072,
                                         r=8, p=1, maxmem=256*1024*1024).hex()
    with sqlite3.connect('file:/var/lib/roocore/roocore.sqlite3?mode=rw', uri=True) as db:
        row = db.execute('SELECT id, username FROM users WHERE admin=1').fetchone()
        if not row:
            raise SystemExit('No owner account found')
        db.execute('UPDATE users SET password=?, enabled=1 WHERE id=?', (hashed, row[0]))
        db.execute('DELETE FROM sessions WHERE user_id=?', (row[0],))
        db.execute('DELETE FROM recovery_tokens WHERE user_id=?', (row[0],))
        db.execute('DELETE FROM email_login_challenges WHERE user_id=?', (row[0],))
        db.execute('DELETE FROM auth_throttle WHERE key=?', ('login-'+hashlib.sha256(row[1].encode()).hexdigest(),))
        db.execute('DELETE FROM auth_throttle WHERE key IN (?, ?)', ('otp-send-'+row[0], 'otp-cooldown-'+row[0]))
    print('RooCore owner password (shown once):', password)


if __name__ == '__main__':
    main()
