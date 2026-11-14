"""Explicit local operator smoke test; no public send-email endpoint."""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from app.mailer import MailConfig, MailError, MailgunMailer
from app.email_templates import render


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="/etc/roocore/mail.json")
    parser.add_argument("--test", action="store_true", required=True)
    args = parser.parse_args()
    try:
        config = MailConfig.load(args.config)
        message = render('test')
        message_id = MailgunMailer(config).send(config.alert_to, message.subject, message.text,
                                               html=message.html, category="test")
    except (MailError, OSError, ValueError) as error:
        # Config errors can contain values; do not echo them.
        print(str(error) if isinstance(error, MailError) else "Invalid mail configuration", file=sys.stderr)
        return 1
    print("Mailgun accepted test message:", message_id)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
