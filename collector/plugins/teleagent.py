"""Read TeleAgent points and today's usage from the official portal APIs."""
from datetime import datetime, timedelta, timezone
from decimal import Decimal, InvalidOperation
from pathlib import Path
from urllib.parse import urlencode

from common import CollectionError, fingerprint, get_json, read_json, scrub

LABEL = "TeleAgent"
BASE = "https://agent.teleai.com.cn/meter/super-agent/user/portal"


def amount(value):
    if isinstance(value, bool) or not isinstance(value, (str, int, float, Decimal)):
        raise CollectionError("invalid_points")
    try:
        number = Decimal(str(value))
        # Match the firmware's bounded, nonnegative decimal strings.
        if not number.is_finite() or number < 0 or number >= Decimal("1e20") or (number and number.adjusted() < -20):
            raise CollectionError("invalid_points")
        result = format(number, "f")
        if "." in result:
            result = result.rstrip("0").rstrip(".")
        if len(result) > 23:
            raise CollectionError("invalid_points")
        return "0" if number == 0 else result
    except (InvalidOperation, ValueError):
        raise CollectionError("invalid_points") from None


def collect(config, secrets):
    try:
        auth = read_json(Path(config["auth_file"]).expanduser())
    except (OSError, ValueError, KeyError):
        raise CollectionError("credentials_missing") from None
    token = auth.get("operation_token")
    user_id = auth.get("user_id")
    if not isinstance(token, str) or not token or any(ord(c) < 32 or ord(c) > 126 for c in token):
        raise CollectionError("credentials_missing")
    headers = {"X-Token": token, "Accept": "application/json"}
    if user_id is not None:
        if not isinstance(user_id, str) or not user_id or any(ord(c) < 32 or ord(c) > 126 for c in user_id):
            raise CollectionError("credentials_missing")
        headers["userId"] = user_id
    # Evaluate the date once, in the site's UTC+8 day, for all usage clients/models.
    day = datetime.now(timezone(timedelta(hours=8))).date().isoformat()

    def request(path):
        raw = get_json(BASE + path, headers=headers, timeout=config.get("timeout", 30), proxy=config.get("proxy"))
        code = raw.get("code")
        if isinstance(code, bool) or str(code) not in ("0", "200"):
            raise CollectionError("auth" if str(code) in ("401", "403") else "business_error")
        if not isinstance(raw.get("data"), dict):
            raise CollectionError("invalid_response")
        return raw["data"]

    balance = request("/points/detail")
    usage = request("/usage/summary?" + urlencode({"startDate": day, "endDate": day}))
    try:
        points = {"balance": amount(balance["pointsBalance"]),
                  "spent_today": amount(usage["todayPointsUsed"]),
                  "added_today": amount(usage["todayPointsAdded"])}
    except KeyError:
        raise CollectionError("invalid_response") from None
    # Preserve point groups/expiry and task/call counts for future displays.
    return {"identity": fingerprint("teleagent:" + (user_id or config["id"])), "points": points,
            "details": scrub({"day": day, "points": balance, "usage": usage}, [token, user_id])}
