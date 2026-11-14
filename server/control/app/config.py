from dataclasses import dataclass
import os
from pathlib import Path

def secret(name: str) -> str:
    value = (Path(os.environ.get("SECRET_DIR", "/etc/roocore/secrets")) / name).read_text().strip()
    if len(value) < 32:
        raise ValueError(f"{name} must contain at least 32 characters")
    return value


@dataclass(frozen=True)
class Settings:
    database_url: object
    admin_user: str
    admin_password: str
    mqtt_password: str
    mqtt_host: str = "127.0.0.1"
    mqtt_port: int = 18883
    # Display freshness only. This is NOT a motor watchdog or a hardware limit.
    stale_seconds: int = 15
    retention_days: int = 30
    auth_secret: str = ''

    @classmethod
    def from_env(cls):
        return cls(
            database_url="sqlite:////var/lib/roocore/roocore.sqlite3",
            admin_user=os.environ.get("ADMIN_USER", "admin"),
            admin_password=secret("admin_password"),
            mqtt_password=secret("mqtt_reader_password"),
            mqtt_host="127.0.0.1",
            auth_secret=secret('auth_secret'),
        )
