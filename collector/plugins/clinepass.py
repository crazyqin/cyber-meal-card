from common import CollectionError, fingerprint, get_json, secret, window

LABEL = "ClinePass"


def collect(config, secrets):
    key = secret(config, secrets, "api_key")
    raw = get_json("https://api.cline.bot/api/v1/users/me/plan/usage-limits",
                   headers={"Authorization": "Bearer " + key}, timeout=config.get("timeout", 30), proxy=config.get("proxy"))
    if raw.get("success") is not True:
        raise CollectionError("invalid_response")
    minutes = {"five_hour": 300, "weekly": 10080, "monthly": 43200}
    windows = [window(row["percentUsed"], minutes[row["type"]], row.get("resetsAt"))
               for row in raw.get("data", {}).get("limits", []) if row.get("type") in minutes]
    if not windows:
        raise CollectionError("no_windows")
    return {"identity": fingerprint(key), "plan": "ClinePass", "windows": windows, "details": raw}
