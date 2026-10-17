"""Schema 2 device contract. Reject a plugin row before caching or publishing it."""
import json
import math
import re

from common import CollectionError, epoch


def validate(row):
    def require(condition):
        if not condition:
            raise CollectionError("invalid_summary")

    def text(value, limit):
        return isinstance(value, str) and 0 < len(value) <= limit and value.isascii() and value.isprintable()

    def number(value):
        return not isinstance(value, bool) and isinstance(value, (int, float)) and math.isfinite(value)

    def stamp(value):
        return value is None or (number(value) and int(value) == value and 0 <= value <= 4102444799)

    def money(value, signed=False, fixed=False):
        pattern = (r"-?" if signed else "") + (r"[0-9]{1,15}\.[0-9]{2}" if fixed else r"[0-9]+(?:\.[0-9]+)?")
        return text(value, 23) and bool(re.fullmatch(pattern, value))

    require(text(row.get("name"), 64) and text(row.get("label", row.get("provider")), 23))
    if "plan" in row:
        require(text(row["plan"], 31))
    checked, collected = epoch(row.get("checked_at")), epoch(row.get("collected_at"))
    require(checked is not None and checked > 0 and row.get("status") in ("ok", "error"))
    require((collected is not None and 0 < collected <= checked) or
            (row["status"] == "error" and row.get("collected_at") is None))
    if collected is None:
        return
    windows, balances = row.get("windows", []), row.get("balances", [])
    require(isinstance(windows, list) and len(windows) <= 3)
    require(isinstance(balances, list) and len(balances) <= 2)
    for w in windows:
        require(isinstance(w, dict))
        minutes, used, remaining = w.get("window_minutes"), w.get("used_percent"), w.get("remaining_percent")
        require(number(minutes) and int(minutes) == minutes and 0 < minutes <= 525600)
        require(number(used) and used >= 0 and number(remaining) and 0 <= remaining <= 100)
        require(abs(max(0, 100 - used) - remaining) <= 0.001 and stamp(w.get("resets_at_unix")))
    for b in balances:
        require(isinstance(b, dict) and text(b.get("currency"), 3))
        require(all(money(b.get(k)) for k in ("total", "granted", "topped_up")))
    reset = row.get("reset_credits", {})
    require(isinstance(reset, dict))
    count = reset.get("available_count")
    require(count is None or (number(count) and int(count) == count and 0 <= count <= 1000000))
    require(all(stamp(reset.get(k)) for k in ("latest_expires_at_unix", "next_expires_at_unix")))
    if "points" in row:
        p = row["points"]
        require(isinstance(p, dict) and not windows and not balances and "tokenhub" not in row and "reset_credits" not in row)
        require(all(money(p.get(k)) for k in ("balance", "spent_today", "added_today")))
    if row.get("provider") == "tokenhub":
        t = row.get("tokenhub")
        require(isinstance(t, dict) and t.get("currency") == "CNY" and t.get("remaining_is_estimate") is True)
        require(all(money(t.get(k), fixed=True) for k in ("month_spent_yuan", "range_spent_yuan")))
        if t.get("allocation_yuan", "missing") is None:
            require("estimated_remaining_yuan" in t and t["estimated_remaining_yuan"] is None)
        else:
            require(money(t.get("allocation_yuan"), fixed=True) and money(t.get("estimated_remaining_yuan"), signed=True, fixed=True))
    require(len(json.dumps(row, allow_nan=False).encode()) <= 4096)
