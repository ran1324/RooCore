"""Provision only the OTP verifier secret, never replace an existing key or password."""
import os
from pathlib import Path
import pwd
import secrets

if os.geteuid() != 0:
    raise SystemExit('Run as root on the RooCore server')
path = Path('/etc/roocore/secrets/auth_secret')
if not path.exists():
    user = pwd.getpwnam('roocore')
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL | os.O_NOFOLLOW, 0o600)
    with os.fdopen(fd, 'w') as handle:
        os.fchown(handle.fileno(), user.pw_uid, user.pw_gid)
        handle.write(secrets.token_urlsafe(48) + '\n')
        handle.flush()
        os.fsync(handle.fileno())
if path.is_symlink() or path.stat().st_mode & 0o077 or len(path.read_text().strip()) < 32:
    raise SystemExit('OTP secret must be a private regular file with at least 32 characters')
print('OTP verifier secret is provisioned; value was not printed')
