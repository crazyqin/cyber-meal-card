[简体中文](README.zh_CN.md) · **English**

# Modular collector

ChatGPT, TokenHub, ClinePass, DeepSeek and TeleAgent are independent plugins. Start with
[`config.example.json`](config.example.json) and the
[setup and protocol guide](../docs/development/subscription-viewer.md).

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt
.venv/Scripts/python collect.py init --providers chatgpt
.venv/Scripts/python collect.py run --once
```

Edit ignored `config.local.json` and `secrets.local.json` for your machine.
Double-click `start.cmd` for continuous collection or `configure.cmd` for interactive setup. Each computer gets a unique
ID and uploads its own summary and details to COS. Configure COS before uploading.

Firmware builds automatically embed the local COS configuration; provider keys
stay on the collector computer. Use separate read-only `device_cos` credentials
when available. Never share local secrets, account data or private firmware.

For Linux without Docker, use the [native deployment bundle guide](linux/README.md).

## Background operation and recovery

On Windows, run `powershell -File .\service.ps1 install` from this directory to
install a task for the current user. It starts at login, runs without a console,
and restarts the supervisor after an unexpected exit. Use `status`, `start`,
`stop` or `remove` instead of `install` to manage that task. Stop an existing
foreground collector before installing it. Collection runs only while that user
is logged in; Windows sleep also pauses collection.

`start.cmd` and the scheduled task use `supervise.py`. Each cycle has a default
180-second deadline (`cycle_timeout_seconds`, 30..3600); a hung cycle is terminated
and retried. The interval defaults to 300 seconds. Logs rotate at 1 MiB with three
backups in `state/collector.log`. The next cycle reloads configuration. Keep
`collect.py run --once` for manual checks while background collection is stopped.

Malformed credential JSON is reported as `secrets_unreadable`; the last valid
snapshots remain intact and the supervisor retries. A valid plugin summary must
pass the schema 2 contract before replacing cached data. Invalid rows become
account errors, and duplicate provider/account identities on one computer are
merged, preferring successful data. COS session tokens follow the same
environment-variable/file resolution as the other credentials.

TokenHub executes downloaded scripts in a capability-free Windows AppContainer,
or in a Linux bubblewrap filesystem/network namespace, with an additional Node
permission policy. Runtime code is readable; account files and collected state
are outside that boundary. No unrestricted fallback is provided. Linux needs
`bubblewrap` and working unprivileged user namespaces; see the Linux guide.
