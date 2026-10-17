"""Shared, provider-independent configuration, storage and HTTP helpers."""
from __future__ import annotations

from datetime import datetime, timezone
import hashlib
import json
import math
import os
from pathlib import Path
import re
import tempfile

ROOT = Path(__file__).resolve().parent
ID = re.compile(r"[A-Za-z0-9_-][A-Za-z0-9_.~-]{0,63}\Z")


class CollectionError(RuntimeError):
    def __init__(self, code="unavailable"):
        self.code = code
        super().__init__(code)


def utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def epoch(value):
    if isinstance(value, bool) or value is None:
        return None
    try:
        if isinstance(value, (float, int)):
            return int(value) if math.isfinite(value) and 0 <= value <= 4102444799 else None
        dt = datetime.fromisoformat(str(value).replace("Z", "+00:00"))
        return int(dt.timestamp()) if dt.tzinfo else None
    except (ValueError, TypeError, OverflowError):
        return None


def read_json(path):
    with Path(path).open(encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("Expected a JSON object")
    return value


def atomic_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    fd, temp = tempfile.mkstemp(prefix=".write-", dir=path.parent)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as stream:
            json.dump(value, stream, ensure_ascii=False, allow_nan=False, indent=2)
            stream.write("\n")
        os.replace(temp, path)
    finally:
        Path(temp).unlink(missing_ok=True)


def secret(config, secrets, field):
    ref = config.get(field + "_ref")
    value = os.environ.get(ref, secrets.get(ref, "")) if isinstance(ref, str) else ""
    if not isinstance(value, str) or not value or any(ord(c) < 32 for c in value):
        raise CollectionError("credentials_missing")
    return value


def fingerprint(value):
    return hashlib.sha256(value.encode()).hexdigest()[:24]


def window(used, minutes, reset):
    if isinstance(used, bool) or not isinstance(used, (int, float)) or not math.isfinite(used) or used < 0:
        raise CollectionError("invalid_window")
    if isinstance(minutes, bool) or not isinstance(minutes, (int, float)) or int(minutes) != minutes or not 0 < minutes <= 525600:
        raise CollectionError("invalid_window")
    return {"used_percent": used, "remaining_percent": max(0, 100 - used),
            "window_minutes": int(minutes), "resets_at_unix": epoch(reset)}


def scrub(value, forbidden=(), depth=0):
    """Keep extensible business data; exclude auth/PII keys and actual secrets."""
    if depth > 12:
        return None
    if isinstance(value, dict):
        result = {}
        for key, item in value.items():
            normalized = re.sub(r"[^a-z]", "", str(key).lower())
            if any(word in normalized for word in ("password", "secret", "authorization", "cookie", "accesstoken", "refreshtoken", "operationtoken", "usertoken", "idtoken", "apikey", "satoken", "email", "accountid", "userid")):
                continue
            result[str(key)] = scrub(item, forbidden, depth + 1)
        return result
    if isinstance(value, list):
        return [scrub(item, forbidden, depth + 1) for item in value[:10000]]
    if isinstance(value, str):
        for token in forbidden:
            if isinstance(token, str) and len(token) >= 6:
                value = value.replace(token, "[redacted]")
        return value[:65536]
    if isinstance(value, float) and not math.isfinite(value):
        return None
    return value if value is None or isinstance(value, (bool, int, float)) else str(value)


def get_json(url, *, headers=None, timeout=30, proxy=None):
    # No redirects: authorization stays on the adapter's fixed origin.
    import requests
    try:
        with requests.Session() as session:
            session.trust_env = False
            response = session.get(url, headers=headers, timeout=timeout, allow_redirects=False,
                                   proxies={"http": proxy, "https": proxy} if proxy else {})
            if response.status_code != 200:
                raise CollectionError("auth" if response.status_code in (401, 403) else "http_" + str(response.status_code))
            if len(response.content) > 2_000_000:
                raise CollectionError("response_too_large")
            result = response.json()
            if not isinstance(result, dict):
                raise CollectionError("invalid_response")
            return result
    except CollectionError:
        raise
    except Exception:
        raise CollectionError("network_or_json") from None
