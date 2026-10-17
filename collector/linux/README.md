[简体中文](README.zh_CN.md) · **English**

# Linux collector deployment

This bundle runs directly on Linux, without Docker. It supports x86_64 and ARM64
on glibc 2.28 or newer (for example Ubuntu 20.04+ or Debian 11+). Initial installation
needs Bash, curl, tar, coreutils, CA certificates and internet access. It downloads
pinned Python 3.12, Node 24 and Codex CLI runtimes with SHA-256 verification, then
installs Python/npm dependencies locally. It needs no preinstalled Python/Node/Codex
and does not modify system packages. Alpine/musl and 32-bit systems are not supported.

## Deploy

Extract the archive into a permanent directory owned by the intended user. Run:

```bash
tar -xzf ai-collector-linux-private-20261017-teleagent.tar.gz
cd ai-collector-linux
bash install.sh
bash collector.sh login
bash collector.sh check
bash collector.sh start
bash collector.sh status
```

`login` displays a device authorization URL/code; finish signing in with the intended
ChatGPT account in your browser. If device login is disabled for the account, follow
the [official authentication guide](https://learn.chatgpt.com/docs/auth). Logins live
under this deployment's `accounts/`, separate from any existing Codex installation.
This package contains no existing ChatGPT/TokenHub/TeleAgent login or provider API key.

The **private** package includes the current COS bucket, region, prefix and credentials
in `settings/cos.local.json`. Keep the archive private. Installation initializes
`config.local.json` and `secrets.local.json` without displaying the credential values.
Re-running installation preserves existing configuration. The public build contains
no COS credentials; configure them with `bash collector.sh configure`.

ChatGPT is enabled by default; other modules are available but disabled. A fresh
computer gets a unique collector ID on its first collection, so it cannot overwrite
the original computer's objects. The firmware discovers the new collector automatically.
Do not copy `state/` or login directories from a running deployment.

## Other platforms and accounts

```bash
bash collector.sh configure          # Enable modules and enter local keys
bash collector.sh tokenhub-auth      # Enter TokenHub Satoken without echo
bash collector.sh teleagent-auth     # Enter TeleAgent operationToken without echo
bash collector.sh restart            # Apply configuration changes
```

For TokenHub, first enable its module in `configure`, then save its Satoken with
`tokenhub-auth`. The installer includes its local Node/WAF dependencies. An expired
TokenHub session needs a replacement Satoken. ClinePass and DeepSeek keys are entered
only on the collecting computer. Use the same explicit `account_id` when different
keys refer to the same account across computers.

For TeleAgent (the Star Points portal), log in at `https://agent.teleai.com.cn/`, then copy
the URL-decoded `operationToken` cookie through your browser's developer tools. Use
`teleagent-auth` to save it privately, enable `teleagent-main` in `configure`, and restart.
The firmware shows points balance, today's consumption and today's additions. The collector
keeps point groups/expiration and task/call counts in private details. Sessions can expire;
update the credential when an `auth` error occurs. Use the same explicit `account_id` for the
same TeleAgent account across computers. Do not paste login credentials into chat or firmware.

For an existing deployment, stop the collector, extract the new bundle into a separate sibling
directory, and copy only `app/` and `deploy.py` from the new directory over the existing ones.
This update preserves `.runtime/`, `.venv/`, `config.local.json`, `secrets.local.json`, `settings/`,
`accounts/` and `state/`; no runtime installation is needed for this adapter. If the existing
configuration has no TeleAgent row, add it before running `teleagent-auth`:

```bash
bash collector.sh cli add teleagent --id teleagent-main --name teleagent --account-id teleagent-personal --auth-file settings/teleagent-auth.local.json
bash collector.sh teleagent-auth
bash collector.sh check
bash collector.sh start
```

Newly added rows are enabled by default. Never replace your existing settings with the new
bundle's preset or copy machine state between computers.

To add another ChatGPT login:

```bash
bash collector.sh cli add chatgpt --id work --name work --codex-home accounts/work
```

Then run `bash collector.sh login work` and restart. The bundled Codex is discovered automatically. Display
names must be printable ASCII. All paths in account configuration are relative to
the deployment's `config.local.json`; use separate directories for different logins.

## Control and troubleshooting

```bash
bash collector.sh start       # Detached background process; SSH logout is fine
bash collector.sh stop        # Stop only this directory's verified process group
bash collector.sh restart
bash collector.sh status      # Process status and most recent collection/upload result
bash collector.sh logs        # Last 100 log lines
bash collector.sh check       # One live collection and upload, when background is stopped
bash collector.sh run         # Foreground mode, for a supervisor such as systemd
bash collector.sh runtime-check
```

Collection runs every five minutes. `accounts=... errors=0 upload=ok` means that run
succeeded. `state/status.json`, `latest.json` and `details.json` retain local results;
logs are in `state/collector.log`. A source error does not refresh the old source timestamp.
`check` uses the same state lock as the background process: stop it before a manual
one-shot check. `runtime-check` verifies local dependencies, not account authentication
or live API availability. Restart after editing module configuration.

`start` survives SSH logout but is not an automatic reboot service. If desired, use
a systemd unit whose `WorkingDirectory` is this deployment directory and whose
`ExecStart` is `/absolute/path/.venv/bin/python /absolute/path/deploy.py run`.
Configure the service under the intended user's account and keep file access private.

The first install downloads runtimes from GitHub/nodejs.org and dependencies from
PyPI/npm; it is not an offline runtime image. curl honors download proxy environment
variables. Runtime API calls still need network access to the selected providers and
COS; configure the per-provider/COS proxy fields when needed. On failed downloads,
rerun installation to reuse verified cached archives. If a different runtime already
exists, the installer preserves it and asks for a fresh deployment directory.

All generated credentials use private file modes. No root login, Docker daemon,
system-wide Node/Python installation or provider key in firmware is required.
`manifest.json` records the deployment files and their hashes; `runtime-lock.json`
records platform-specific download URLs and hashes. The package has passed archive,
shell-syntax and host logic tests; a real Linux installation is still a separate check.
