from decimal import Decimal, InvalidOperation
from common import CollectionError, fingerprint, get_json, secret

LABEL = "DeepSeek"


def collect(config, secrets):
    key = secret(config, secrets, "api_key")
    raw = get_json("https://api.deepseek.com/user/balance", headers={"Authorization": "Bearer " + key},
                   timeout=config.get("timeout", 30), proxy=config.get("proxy"))
    balances = []
    for item in raw.get("balance_infos", []):
        if item.get("currency") not in ("CNY", "USD"):
            continue
        row = {"currency": item["currency"]}
        for source, target in (("total_balance", "total"), ("granted_balance", "granted"), ("topped_up_balance", "topped_up")):
            try:
                amount = Decimal(str(item[source]))
                if not amount.is_finite() or amount < 0:
                    raise ValueError
            except (InvalidOperation, ValueError, KeyError):
                raise CollectionError("invalid_balance") from None
            row[target] = format(amount, "f")
        balances.append(row)
    if not balances or not isinstance(raw.get("is_available"), bool):
        raise CollectionError("no_balances")
    return {"identity": fingerprint(key), "plan": "API", "balances": balances,
            "available": raw["is_available"], "details": raw}
