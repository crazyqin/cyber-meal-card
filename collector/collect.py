#!/usr/bin/env python3
"""Modular quota collector. Run --help; credentials never enter console output."""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import copy
import importlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import sys
import time
import uuid

from common import CollectionError, ID, ROOT, atomic_json, fingerprint, read_json, scrub, secret, utc_now
from summary import validate as validate_summary


def available_plugins():
    return sorted(p.stem for p in (ROOT / "plugins").glob("*.py") if not p.stem.startswith("_"))


def load_config(path):
    path = Path(path).resolve()
    config = read_json(path)
    if config.get("schema_version") != 2:
        raise ValueError("Unsupported configuration version")
    if not isinstance(config.get("interval_seconds"), int) or not 30 <= config["interval_seconds"] <= 86400:
        raise ValueError("interval_seconds must be 30..86400")
    budget = config.get("cycle_timeout_seconds", 180)
    if isinstance(budget, bool) or not isinstance(budget, int) or not 30 <= budget <= 3600:
        raise ValueError("cycle_timeout_seconds must be 30..3600")
    if config.get("collector_id", "auto") != "auto" and not valid_id(config["collector_id"]):
        raise ValueError("Invalid collector_id")
    accounts = config.get("accounts")
    if not isinstance(accounts, list) or len(accounts) > 32:
        raise ValueError("At most 32 accounts per collector")
    ids = set()
    for row in accounts:
        if not isinstance(row, dict) or not valid_id(row.get("id")) or row["id"] in ids:
            raise ValueError("Account ids must be unique URI-safe identifiers")
        ids.add(row["id"])
        if row.get("account_id") is not None and not valid_id(row["account_id"]):
            raise ValueError("Invalid stable account_id")
        if not valid_id(row.get("provider")) or not re.fullmatch(r"[a-z][a-z0-9_]{0,31}", row["provider"]):
            raise ValueError("Invalid provider module name")
        if not isinstance(row.get("enabled", True), bool):
            raise ValueError("enabled must be true or false")
        if not isinstance(row.get("timeout", 30), (int, float)) or not 1 <= row.get("timeout", 30) <= 300:
            raise ValueError("Adapter timeout must be 1..300 seconds")
        name = row.get("name", row["id"])
        if not isinstance(name, str) or not name or len(name) > 64 or not name.isascii() or not name.isprintable():
            raise ValueError("Display names must be printable ASCII (1..64 characters)")
        for key in ("auth_file", "codex_home"):
            if row.get(key):
                row[key] = str(resolve(path.parent, row[key]))
    config["_path"] = path
    config["_state"] = resolve(path.parent, config.get("state_dir", "state"))
    config["_secrets"] = resolve(path.parent, config.get("secrets_file", "secrets.local.json"))
    return config


def valid_id(value):
    return isinstance(value, str) and bool(ID.fullmatch(value)) and ".." not in value


def resolve(base, value):
    value = Path(value).expanduser()
    return value.resolve() if value.is_absolute() else (base / value).resolve()


def machine_identity():
    if os.name == "nt":
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"SOFTWARE\Microsoft\Cryptography") as key:
            value = winreg.QueryValueEx(key, "MachineGuid")[0]
    elif Path("/etc/machine-id").is_file():
        value = Path("/etc/machine-id").read_text().strip()
    else:
        value = platform.node() + str(uuid.getnode())
    return fingerprint(str(value))


def collector_id(config):
    if config.get("collector_id", "auto") != "auto":
        return config["collector_id"]
    path = config["_state"] / "identity.json"
    machine = machine_identity()
    try:
        stored = read_json(path)
    except (OSError, ValueError):
        stored = {}
    if stored.get("machine") != machine or not valid_id(stored.get("id")):
        hostname = re.sub(r"[^A-Za-z0-9_-]", "-", platform.node())[:35] or "computer"
        stored = {"machine": machine, "id": hostname + "-" + uuid.uuid4().hex[:12]}
        atomic_json(path, stored)
    return stored["id"]


@contextmanager
def run_lock(directory):
    directory.mkdir(parents=True, exist_ok=True)
    with (directory / ".collector.lock").open("a+b") as handle:
        if handle.tell() == 0:
            handle.write(b"0")
            handle.flush()
        handle.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(handle.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(handle.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            raise ValueError("Another collector is using this state directory") from None
        yield


def collect_report(config, secrets, previous=None, previous_details=None):
    previous = previous or {}
    old = {r["local_id"]: r for r in previous.get("accounts", []) if "local_id" in r}
    old_details = {r["local_id"]: r for r in (previous_details or {}).get("accounts", []) if "local_id" in r}
    forbidden = list(secrets.values())
    for section in [config.get("cos", {}), config.get("device_cos", {}), *config["accounts"]]:
        for key, ref in section.items():
            if key.endswith("_ref") and isinstance(ref, str) and os.environ.get(ref):
                forbidden.append(os.environ[ref])
    records, details = [], []
    for row in config["accounts"]:
        if not row.get("enabled", True):
            continue  # Only enabled provider modules are imported.
        checked = utc_now()
        record = {"local_id": row["id"], "provider": row["provider"], "name": row.get("name", row["id"]),
                  "account_id": row.get("account_id", row["id"]), "checked_at": checked}
        base_record = record.copy()
        try:
            module = importlib.import_module("plugins." + row["provider"])
            result = dict(module.collect(row, secrets))
            checked = utc_now()
            record["checked_at"] = checked
            identity = row.get("account_id") or result.pop("identity", None) or row["id"]
            if not valid_id(identity):
                raise CollectionError("invalid_identity")
            raw = scrub(result.pop("details", {}), forbidden)
            # Extended fields stay in details.json, including unknown plugin data.
            detail_row = {**record, "account_id": identity, "status": "ok", "collected_at": checked,
                          "data": scrub(result, forbidden), "source_data": raw}
            record.update(account_id=identity, status="ok", collected_at=checked, label=module.LABEL)
            for key in ("plan", "windows", "balances", "available", "tokenhub", "reset_credits", "points"):
                if key in result:
                    record[key] = result[key][:3] if key == "windows" else result[key][:2] if key == "balances" else result[key]
            # Validate plugin summaries before replacing the last good snapshot.
            validate_summary(record)
            details.append(detail_row)
        except Exception as error:
            code = error.code if isinstance(error, CollectionError) else "adapter_failed"
            if not isinstance(code, str) or not re.fullmatch(r"[a-z0-9_]{1,64}", code):
                code = "adapter_failed"
            # No str(error): upstream/SDK errors may contain auth or URLs.
            cached = old.get(row["id"])
            binding = credential_binding(row, secrets)
            try:
                if cached:
                    validate_summary(cached)
            except (CollectionError, TypeError, ValueError):
                cached = None
            if cached and cached.get("_binding") == binding:
                record = {**copy.deepcopy(cached), "status": "error", "checked_at": checked, "error_code": code}
                prior_detail = old_details.get(row["id"], {})
            else:
                record = {**base_record, "checked_at": checked, "status": "error", "collected_at": None, "error_code": code}
                prior_detail = {}
            details.append({**copy.deepcopy(prior_detail), **{k: v for k, v in record.items() if not k.startswith("_")}})
        record["_binding"] = credential_binding(row, secrets)
        records.append(record)
    # Different local aliases may resolve to the same remote identity. Prefer a
    # successful row and emit that identity once; cross-computer merging is unchanged.
    unique = {}
    for record, detail_row in zip(records, details):
        key = (record["provider"], record["account_id"])
        old_pair = unique.get(key)
        if old_pair is None or (record["status"] == "ok", record.get("collected_at") or "") > \
                (old_pair[0]["status"] == "ok", old_pair[0].get("collected_at") or ""):
            unique[key] = (record, detail_row)
    records = [pair[0] for pair in unique.values()]
    details = [pair[1] for pair in unique.values()]
    report = {"schema_version": 2, "collector_id": collector_id(config), "collected_at": utc_now(),
              "accounts": records}
    detail = {**report, "accounts": details}
    return report, detail


def credential_binding(row, secrets):
    ref = row.get("api_key_ref", "")
    value = json.dumps(row, sort_keys=True) + str(os.environ.get(ref) or secrets.get(ref, ""))
    auth_path = row.get("auth_file")
    if row.get("codex_home"):
        auth_path = str(Path(row["codex_home"]) / "auth.json")
    if auth_path:
        try:
            value += fingerprint(Path(auth_path).read_text(encoding="utf-8-sig"))
        except OSError:
            value += "unreadable"
    return fingerprint(value)


def public_report(report):
    result = copy.deepcopy(report)
    for row in result["accounts"]:
        row.pop("_binding", None)
    return result


def publish(config, secrets, report, details):
    from qcloud_cos import CosConfig, CosS3Client
    import logging
    import requests
    logging.getLogger("qcloud_cos").setLevel(logging.CRITICAL)
    cos = config.get("cos", {})
    bucket, region, prefix = [cos.get(k, "") for k in ("bucket", "region", "prefix")]
    if not re.fullmatch(r"[a-z0-9-]+-\d+", bucket) or not re.fullmatch(r"[a-z0-9-]+", region):
        raise CollectionError("cos_config")
    if not prefix or len(prefix) > 128 or any(not re.fullmatch(r"[A-Za-z0-9_.~-]+", part) or ".." in part for part in prefix.split("/")):
        raise CollectionError("cos_prefix")
    identity = report["collector_id"]
    data = json.dumps(public_report(report), ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode()
    detail_data = json.dumps(details, ensure_ascii=False, allow_nan=False, separators=(",", ":")).encode()
    if len(data) >= 24576 or len(detail_data) > 2_000_000:
        raise CollectionError("report_too_large")
    with requests.Session() as session:
        session.trust_env = False
        client = CosS3Client(CosConfig(Region=region, SecretId=secret(cos, secrets, "secret_id"),
            SecretKey=secret(cos, secrets, "secret_key"), Token=secret(cos, secrets, "token") if cos.get("token_ref") and
                (os.environ.get(cos["token_ref"]) or secrets.get(cos["token_ref"])) else None,
            Scheme="https", Timeout=30, Proxies={"http": cos["proxy"], "https": cos["proxy"]} if cos.get("proxy") else {}),
            retry=2, session=session)
        # Independent objects per machine. No shared index and no read/modify/write race.
        # Detail upload precedes current snapshot; source timestamps connect them.
        for filename, body in (("details.json", detail_data), ("latest.json", data)):
            client.put_object(Bucket=bucket, Key=f"{prefix}/{identity}/{filename}", Body=body,
                ContentType="application/json; charset=utf-8", CacheControl="no-store", EnableMD5=True)


def run(config, once=False, no_upload=False):
    with run_lock(config["_state"]):
        while True:
            started = time.monotonic()
            try:
                secrets = read_json(config["_secrets"])
            except (OSError, ValueError):
                # Editors can briefly replace a file with partial JSON. Never
                # overwrite the last good snapshots or publish a fabricated zero.
                atomic_json(config["_state"] / "status.json", {"checked_at": utc_now(),
                    "upload": "skipped", "error_code": "secrets_unreadable", "running": True})
                print(f"{utc_now()} error=secrets_unreadable retry=30", flush=True)
                if once:
                    return 1
                time.sleep(30)
                continue
            try:
                previous = read_json(config["_state"] / "latest.local.json")
            except (OSError, ValueError):
                previous = {}
            try:
                previous_details = read_json(config["_state"] / "details.json")
            except (OSError, ValueError):
                previous_details = {}
            report, detail = collect_report(config, secrets, previous, previous_details)
            atomic_json(config["_state"] / "latest.local.json", report)
            atomic_json(config["_state"] / "latest.json", public_report(report))
            atomic_json(config["_state"] / "details.json", detail)
            failures = sum(r["status"] != "ok" for r in report["accounts"])
            state = "skipped" if no_upload else "ok"
            upload_code = None
            if not no_upload:
                try:
                    publish(config, secrets, report, detail)
                except Exception as error:
                    state = "error"
                    upload_code = error.code if isinstance(error, CollectionError) else "upload_failed"
            atomic_json(config["_state"] / "status.json", {"checked_at": utc_now(), "accounts": len(report["accounts"]),
                "collection_errors": failures, "upload": state, "upload_error_code": upload_code,
                "sources": [{"provider": r["provider"], "status": r["status"], "error_code": r.get("error_code")}
                            for r in report["accounts"]], "collector_id": report["collector_id"], "running": True})
            print(f"{utc_now()} accounts={len(report['accounts'])} errors={failures} upload={state}", flush=True)
            for row in report["accounts"]:
                if row["status"] != "ok":
                    print(f"  {row['local_id']}: {row['error_code']}", flush=True)
            if once:
                return int(bool(failures or state == "error"))
            delay = max(1, config["interval_seconds"] - (time.monotonic() - started))
            time.sleep(delay)


def initialize(args):
    config_path = args.config.resolve()
    secrets_path = config_path.with_name("secrets.local.json")
    if config_path.exists() or secrets_path.exists():
        raise ValueError("Local configuration already exists; edit it or select a new directory")
    values, cos = {}, {}
    if args.key_file:
        lines = args.key_file.read_text(encoding="utf-8-sig").splitlines()
        names = {"clinepass": "CLINE_API_KEY", "deepseek": "DEEPSEEK_API_KEY",
                 "secret_id": "COS_SECRET_ID", "secret_key": "COS_SECRET_KEY"}
        for line in lines:
            line = line.strip()
            parts = re.split(r"[:：]", line, maxsplit=1)
            if len(parts) == 2 and parts[0].lower() in names:
                values[names[parts[0].lower()]] = parts[1].strip()
            elif re.fullmatch(r"[a-z0-9-]+-\d{6,}", line):
                cos["bucket"] = line
            elif re.fullmatch(r"[a-z]{2}-[a-z]+(?:-\d+)?", line):
                cos["region"] = line
    cos.update(prefix=args.prefix, secret_id_ref="COS_SECRET_ID", secret_key_ref="COS_SECRET_KEY", token_ref="COS_SESSION_TOKEN")
    accounts = []
    enabled = args.providers.split(",")
    for provider in ("chatgpt", "clinepass", "deepseek", "tokenhub", "teleagent"):
        row = {"id": provider + "-main", "provider": provider, "name": provider, "enabled": provider in enabled}
        if provider in ("clinepass", "deepseek"):
            row["api_key_ref"] = "CLINE_API_KEY" if provider == "clinepass" else "DEEPSEEK_API_KEY"
        elif provider == "chatgpt":
            row.update(codex_home=str(Path(os.environ.get("CODEX_HOME", Path.home() / ".codex"))), collect_usage=True)
        elif provider == "tokenhub":
            row["auth_file"] = "tokenhub-auth.local.json"
            if args.tokenhub_auth:
                target = config_path.parent / row["auth_file"]
                if target.exists():
                    raise ValueError("TokenHub credentials already exist")
                atomic_json(target, read_json(args.tokenhub_auth))
            if shutil.which("node"):
                row["node_bin"] = shutil.which("node")
        elif provider == "teleagent":
            row["auth_file"] = "teleagent-auth.local.json"
        accounts.append(row)
    config = {"schema_version": 2, "collector_id": "auto", "interval_seconds": 300,
        "state_dir": "state", "secrets_file": secrets_path.name, "cos": cos, "accounts": accounts}
    atomic_json(secrets_path, values)
    atomic_json(config_path, config)
    print("Local configuration created (credential values hidden).")


def configure(path):
    """Small terminal wizard; secret input is hidden and existing values stay hidden."""
    import getpass
    config = read_json(path)
    # Validate the current file before editing; keep raw relative paths in the file.
    loaded = load_config(path)
    secrets = read_json(loaded["_secrets"])
    print("Choose collectors / 选择采集模块 (Enter keeps the current setting).")
    for row in config["accounts"]:
        current = row.get("enabled", True)
        answer = input(f"{row['id']} ({row['provider']}) [{'Y' if current else 'N'}] y/n: ").strip().lower()
        if answer not in ("", "y", "n"):
            raise ValueError("Use y or n")
        row["enabled"] = current if not answer else answer == "y"
        if row["enabled"] and row.get("api_key_ref"):
            ref = row["api_key_ref"]
            value = getpass.getpass(f"{ref} (Enter keeps saved value): ").strip()
            if value:
                secrets[ref] = value
    print("COS configuration / 腾讯 COS 配置 (Enter keeps saved value).")
    for field in ("bucket", "region", "prefix"):
        value = input(f"{field} [{config['cos'].get(field, '')}]: ").strip()
        if value:
            config['cos'][field] = value
    for field in ('secret_id_ref', 'secret_key_ref', 'token_ref'):
        ref = config['cos'].get(field)
        if ref:
            value = getpass.getpass(f"{ref} (Enter keeps saved value): ").strip()
            if value:
                secrets[ref] = value
    atomic_json(loaded["_secrets"], secrets)
    atomic_json(path, config)
    print("Configuration saved. Restart the collector / 已保存，请重启采集器。")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.local.json")
    sub = parser.add_subparsers(dest="command", required=True)
    init = sub.add_parser("init", help="Create editable config and separate local secrets")
    init.add_argument("--key-file", type=Path)
    init.add_argument("--providers", default="chatgpt,clinepass,deepseek,tokenhub")
    init.add_argument("--prefix", default="codex-usage")
    init.add_argument("--tokenhub-auth", type=Path)
    sub.add_parser("list", help="List plugin modules without loading them")
    sub.add_parser("configure", help="Interactive plugin/COS/key configuration (secret input hidden)")
    for name in ("enable", "disable"):
        sub.add_parser(name).add_argument("account_id")
    add = sub.add_parser("add", help="Add an account using an installed plugin")
    add.add_argument("provider")
    add.add_argument("--id", required=True)
    add.add_argument("--name")
    add.add_argument("--account-id", help="Stable identity shared across computers")
    add.add_argument("--codex-home")
    add.add_argument("--auth-file")
    add.add_argument("--api-key-ref")
    running = sub.add_parser("run")
    running.add_argument("--once", action="store_true")
    running.add_argument("--no-upload", action="store_true")
    sub.add_parser("doctor", help="Check config, imports and local credentials without network")
    args = parser.parse_args()
    try:
        if args.command == "list":
            print("\n".join(available_plugins()))
            return 0
        if args.command == "init":
            initialize(args)
            return 0
        if args.command == "configure":
            configure(args.config)
            return 0
        if args.command == "add":
            config = read_json(args.config)
            if args.provider not in available_plugins() or not valid_id(args.id) or any(r['id'] == args.id for r in config['accounts']):
                raise ValueError("Invalid plugin or duplicate account id")
            row = {"id":args.id, "provider":args.provider, "name":args.name or args.id, "enabled":True}
            for field in ('account_id', 'codex_home', 'auth_file', 'api_key_ref'):
                if getattr(args, field):
                    row[field] = getattr(args, field)
            config['accounts'].append(row)
            atomic_json(args.config, config)
            return 0
        if args.command in ("enable", "disable"):
            config = read_json(args.config)
            match = next((r for r in config["accounts"] if r["id"] == args.account_id), None)
            if not match:
                raise ValueError("Unknown account id")
            match["enabled"] = args.command == "enable"
            atomic_json(args.config, config)
            return 0
        config = load_config(args.config)
        if args.command == "doctor":
            secrets = read_json(config["_secrets"])
            for row in config["accounts"]:
                if row.get("enabled", True):
                    importlib.import_module("plugins." + row["provider"])
                    if row.get("api_key_ref") and not (os.environ.get(row["api_key_ref"]) or secrets.get(row["api_key_ref"])):
                        raise ValueError("An enabled adapter is missing a credential")
            print("Configuration and enabled plugins: PASS (no network requests).")
            return 0
        return run(config, args.once, args.no_upload)
    except KeyboardInterrupt:
        print("Stopped.")
        return 130
    except Exception:
        print("Collector could not complete. Check local configuration, dependencies and credentials; values hidden.", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
