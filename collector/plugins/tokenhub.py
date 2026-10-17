"""Reuse the existing browser-free HTTP/WAF transport."""
from datetime import datetime, timedelta, timezone
from decimal import Decimal, ROUND_HALF_UP
import json
from pathlib import Path

from common import CollectionError, fingerprint, read_json

LABEL = "TokenHub"


def collect(config, secrets):
    from tokenhub_runtime.http_protocol import HttpProtocolClient
    auth = read_json(Path(config["auth_file"]).expanduser())
    token = auth.get("satoken")
    if not isinstance(token, str) or not token or any(c in token for c in "\r\n"):
        raise CollectionError("credentials_missing")
    day = datetime.now(timezone(timedelta(hours=8))).date().isoformat()
    with HttpProtocolClient(config.get("timeout", 30), config.get("use_env_proxy", False), config.get("node_bin")) as client:
        status, body = client.fetch(auth, day)
    if status != 200:
        raise CollectionError("auth" if status in (401, 403) else "http_" + str(status))
    if len(body) > 2_000_000:
        raise CollectionError("response_too_large")
    raw = json.loads(body, parse_float=Decimal)
    code = raw.get("code", raw.get("returncode"))
    if code is not None and str(code) not in ("0", "200"):
        raise CollectionError("business_error")
    data = raw if code is None else raw.get("data", raw.get("result"))
    def amount(name, nullable=False):
        value = data[name]
        if value is None and nullable:
            return None
        result = Decimal(str(value))
        if not result.is_finite() or result < 0:
            raise CollectionError("invalid_balance")
        return result
    allocated, spent, today = amount("allocateAmount", True), amount("monthConsumption"), amount("timeRangeConsumption")
    def money(value):
        return None if value is None else format(value.quantize(Decimal("0.01"), rounding=ROUND_HALF_UP), ".2f")
    amounts = {"allocation_yuan": money(allocated), "month_spent_yuan": money(spent),
               "estimated_remaining_yuan": money(None if allocated is None else allocated - spent),
               "range_spent_yuan": money(today), "currency": "CNY", "remaining_is_estimate": True}
    # A rotating login token is not a stable account identity; use configured id.
    return {"identity": fingerprint(config["id"]), "plan": "CNY / estimated", "tokenhub": amounts,
            "details": json.loads(json.dumps(raw, default=str))}
