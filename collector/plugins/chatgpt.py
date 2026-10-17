"""Read-only Codex app-server adapter; never sends a model turn or redeems credits."""
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import threading

from common import CollectionError, epoch, fingerprint, window

LABEL = "ChatGPT"


def executable(config):
    value = config.get("codex_bin") or shutil.which("codex")
    if value:
        return str(value)
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", "")) / "OpenAI/Codex/bin"
        candidates = sorted(base.glob("*/codex.exe"), key=lambda p: p.stat().st_mtime, reverse=True)
        if candidates:
            return str(candidates[0])
    raise CollectionError("codex_not_found")


class AppServer:
    def __init__(self, config):
        self.config, self.messages, self.number = config, queue.Queue(), 0

    def __enter__(self):
        env = os.environ.copy()
        if self.config.get("codex_home"):
            env["CODEX_HOME"] = str(Path(self.config["codex_home"]).expanduser().resolve())
        self.process = subprocess.Popen([executable(self.config), "app-server", "--listen", "stdio://"],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            encoding="utf-8", text=True, env=env,
            creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        def receive():
            try:
                for line in self.process.stdout:
                    try:
                        self.messages.put(json.loads(line))
                    except ValueError:
                        pass
            finally:
                self.messages.put(None)
        self.reader = threading.Thread(target=receive, daemon=True)
        self.reader.start()
        try:
            self.call("initialize", {"clientInfo": {"name": "passport_collector", "version": "2.0.0"},
                                     "capabilities": {"experimentalApi": True}})
            self.send({"method": "initialized"})
        except BaseException:
            self.__exit__()
            raise
        return self

    def send(self, message):
        self.process.stdin.write(json.dumps(message) + "\n")
        self.process.stdin.flush()

    def call(self, method, params=None):
        import time
        self.number += 1
        number = self.number
        self.send({"id": number, "method": method, **({"params": params} if params is not None else {})})
        deadline = time.monotonic() + self.config.get("timeout", 30)
        while True:
            if time.monotonic() >= deadline:
                raise CollectionError("codex_timeout")
            try:
                message = self.messages.get(timeout=max(0, deadline - time.monotonic()))
            except queue.Empty:
                raise CollectionError("codex_timeout") from None
            if message is None:
                raise CollectionError("codex_exited")
            if message.get("id") != number:
                # Never answer server requests with tokens or side effects.
                if "id" in message and "method" in message:
                    self.send({"id": message["id"], "error": {"code": -32601, "message": "Read-only collector"}})
                continue
            if "error" in message:
                raise CollectionError("codex_method_unavailable")
            return message.get("result", {})

    def __exit__(self, *args):
        if self.process.stdin:
            self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.reader.join(timeout=2)
        self.process.stdout.close()


def normalize(limits):
    buckets = limits.get("rateLimitsByLimitId") or {}
    fallback = limits.get("rateLimits")
    if not buckets and isinstance(fallback, dict):
        buckets = {fallback.get("limitId") or "codex": fallback}
    primary = buckets.get("codex") or fallback or next(iter(buckets.values()), {})
    windows = []
    for key in ("primary", "secondary"):
        row = primary.get(key)
        if isinstance(row, dict) and row.get("usedPercent") is not None:
            windows.append(window(row["usedPercent"], row.get("windowDurationMins"), row.get("resetsAt")))
    summary = limits.get("rateLimitResetCredits")
    details = summary.get("credits") if isinstance(summary, dict) else None
    count = summary.get("availableCount") if isinstance(summary, dict) else None
    if isinstance(count, bool) or not isinstance(count, int) or count < 0:
        count = None
    available = [r for r in (details or []) if r.get("status") == "available"]
    latest = max(available, key=lambda r: epoch(r.get("grantedAt")) or -1, default=None)
    expires = [epoch(r.get("expiresAt")) for r in available]
    expires = [n for n in expires if n is not None]
    return {"plan": primary.get("planType") or "ChatGPT", "windows": windows,
            "reset_credits": {"available_count": count, "details_known": isinstance(details, list),
                "latest_granted_at_unix": epoch(latest.get("grantedAt")) if latest else None,
                "latest_expires_at_unix": epoch(latest.get("expiresAt")) if latest else None,
                "next_expires_at_unix": min(expires, default=None)},
            "credits": primary.get("credits"), "limits_by_id": buckets}


def collect(config, secrets):
    with AppServer(config) as server:
        account = server.call("account/read", {"refreshToken": False}).get("account")
        if not isinstance(account, dict) or account.get("type") == "apiKey":
            raise CollectionError("chatgpt_login_required")
        limits = server.call("account/rateLimits/read", {"excludeResetCreditDetails": False})
        optional, failures = {}, []
        if config.get("collect_usage", True):
            try:
                optional["usage"] = server.call("account/usage/read")
            except CollectionError as error:
                failures.append({"section": "usage", "error_code": error.code})
    normalized = normalize(limits)
    # account/read identity is used only to hash; it is never uploaded verbatim.
    identity = limits.get("accountId") or account.get("id") or account.get("accountId") or account.get("email")
    if not identity:
        identity = config["id"]
    normalized.update(identity=fingerprint(str(identity)),
        details={"rate_limits": limits, **optional, "optional_errors": failures})
    return normalized
