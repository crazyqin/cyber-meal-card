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
