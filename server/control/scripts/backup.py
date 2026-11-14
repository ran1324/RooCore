#!/usr/bin/env python3
"""Consistent online SQLite backup, including WAL; local retention is 14 copies."""
from datetime import datetime, timezone
from pathlib import Path
import os
import sqlite3

os.umask(0o077)
folder = Path("/var/backups/roocore")
folder.mkdir(parents=True, exist_ok=True)
stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%S%fZ")
target = folder / (stamp + ".sqlite3")
with sqlite3.connect("file:/var/lib/roocore/roocore.sqlite3?mode=ro", uri=True) as source:
    with sqlite3.connect(target) as backup:
        source.backup(backup)
        if backup.execute("PRAGMA integrity_check").fetchone()[0] != "ok":
            raise SystemExit("Backup integrity check failed")
for path in sorted(folder.glob("*.sqlite3"))[:-14]:
    path.unlink()
print("Verified backup: " + str(target))
