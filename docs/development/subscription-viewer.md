[简体中文](subscription-viewer.zh_CN.md) · **English**

# AI subscription viewer and modular collectors

All providers are collected on computers. ESP32-C3 firmware reads private Tencent COS snapshots;
it makes no ClinePass, DeepSeek, TokenHub, TeleAgent or OpenAI API calls and requires no provider API keys.
The independent application uses `main/quota_app.c`, with service/account pages, source timestamps,
remaining quotas and reset countdowns. The hardware-test demo is not its startup UI.

## Display layout

All accounts use the same quiet 240 x 320 layout: connectivity and battery at the top,
one service title, three metric areas, then source health,
collection age and page number. Every service displays its first metric as a large primary value directly
on the navy background, aligned with the service title. An optional helper line, such as
TokenHub's allocation note, sits beneath that value. Two compact detail cards keep the same
positions across services, including ClinePass and ChatGPT. Quota reset countdowns stay beneath
their values. Dual-currency balances retain separate totals and breakdowns, followed by the
collected DeepSeek API availability. Missing metrics show `--` in a muted slot, without changing
the layout or inventing a zero. Short account/plan metadata sits to the right of the service
title, leaving a 14 px gap before the primary metric label. Generic subtitles such as “remaining quota”
or “balance details”, repeated service names and technical plan labels are omitted. Custom
account aliases remain visible in the compact metadata area except for ChatGPT, whose plan
joins the service title as `ChatGPT Plus` or `ChatGPT Team`: `plus` becomes `Plus`, and
`team`/`business` becomes `Team`. Other known
or future plan strings are retained rather than guessing a tier; missing plans stay absent.
Labels are static; long aliases use ellipses instead of continuous scrolling.

| Service | Metrics |
| --- | --- |
| ClinePass | Remaining five-hour, weekly and monthly quota, with reset countdowns |
| ChatGPT | Remaining quota windows, then available reset credits and the earliest known expiration among available credits |
| DeepSeek | Total, topped-up and granted balances; dual-currency accounts show both totals, their breakdowns and API availability |
| TokenHub | Estimated remaining monthly allocation, monthly spending and today's spending; unlimited allocation is explicit |
| TeleAgent | Points balance, today's points used and today's points added, across all clients/models |

The Deep Ocean Blue theme uses a dark navy background (`#0B1421`), blue panels (`#15253A`),
light text (`#EAF3FF`) and blue accents (`#78B7FF`), with warm orange warnings (`#FFBF80`).
Points identify their unit in the primary metric label; the TeleAgent header has no separate
points badge. Points never use a currency symbol.
Provider titles use distinct accents on the same navy background: ChatGPT mint (`#4FD1A5`),
ClinePass gold (`#F5C16C`), DeepSeek blue (`#7E9BFF`), TeleAgent cyan (`#58C8E8`) and
TokenHub violet (`#C29CFF`). Generic providers and setup titles use the standard light text.
The metric block starts 12 px higher than the previous layout; all services retain the same
card positions relative to that block.

Amounts consistently use the yuan or dollar symbol and two decimal places. Positive amounts
below one cent are marked as less than one cent; very large amounts use explicit scientific
notation. TokenHub's allocation and estimate note appear only next to the estimated balance.
Unknown values stay unknown. Low quota, over-allocation and stale/error states use a warm
warning color. The source timestamp drives collection age, never the device's last download.
Missing or future timestamps are not labeled as freshly collected; records older than seven
days show their collection date in Beijing time (UTC+8). Sync activity appears in the header
without hiding source errors in the footer. ChatGPT shows a countdown to the earliest known
reset-credit expiry, warning within 24 hours. Elapsed expiry says it awaits collector
confirmation; the device does not decrement the collected credit count.
Setup and empty states use the same header/footer. Button actions are unchanged. Manual
account navigation or waking the dimmed screen pauses automatic rotation for 30 seconds;
otherwise rotation runs every 10 seconds. Rotation stops on the settings screen.

The presentation logic is independent of LVGL in `main/quota_presentation.c`; the actual
widgets live in `main/quota_ui.c`. Native screen checks render those same widgets and fonts:

```sh
cmake -S tools/quota_preview -B build/quota-preview -DCMAKE_BUILD_TYPE=Debug
cmake --build build/quota-preview
mkdir -p build/ui-preview
build/quota-preview/quota_preview build/ui-preview
```

The synthetic PPM previews cover service pages, dual currencies, unlimited allocation, missing
reset data, long values, stale/offline/syncing, setup and empty states. They check active-font
glyph coverage, label bounds, static long-text handling and repeated updates. A 32-bit host
build uses the device's 24 KiB LVGL allocator budget; 64-bit builds use 40 KiB for larger host
pointers. Neither host rendering nor its memory figures replace physical-device validation.

## Start the collector

The maintained collector lives in [`collector/`](../../collector/collect.py). From that directory:

```powershell
python -m venv .venv
.venv/Scripts/python -m pip install -r requirements.txt
.venv/Scripts/python collect.py init --key-file ../../key.txt --providers chatgpt,clinepass,deepseek,tokenhub --tokenhub-auth ../../tokenhub_collector/.http-auth.json
.venv/Scripts/python collect.py doctor
.venv/Scripts/python collect.py run --once
.venv/Scripts/python collect.py run
```

`key.txt` import is optional; it recognizes the existing local ClinePass/DeepSeek/COS format.
`config.local.json` contains plugin selection and secret references; `secrets.local.json` contains
credentials. Both are Git-ignored. Environment variables with the same reference name override
file values. Neither credentials nor arbitrary exception text is printed. JSON paths are relative
to the configuration file; `~` expands to the current user's home. `start.cmd` runs continuously;
`check.cmd` runs once. Each run reloads secrets; restart after changing plugin configuration.

Use [`config.example.json`](../../collector/config.example.json) as a portable starting point.
Double-click `configure.cmd` for an interactive module/COS/key wizard; secret input is hidden.
List, enable, disable or add accounts without changing code:

```powershell
python collect.py list
python collect.py disable tokenhub-main
python collect.py enable tokenhub-main
python collect.py add chatgpt --id work --name work --codex-home C:/accounts/work/.codex
```

Only enabled modules are imported. ChatGPT requires an existing ChatGPT login in Codex and a
local Codex executable (`codex_bin` may override discovery). Distinct accounts can use distinct
`codex_home` paths; the collector never logs out or changes the active login. It starts an isolated
app-server process and calls only initialization and account read methods. Optional token usage
failure does not discard valid rate limits. API-key-only Codex login cannot supply ChatGPT limits.

TokenHub reuses the existing tested HTTPS/WAF protocol without launching a browser. When enabled,
install Node.js 24 and run `npm ci` in `collector/tokenhub_runtime/waf_runtime`. `TOKENHUB_NODE`
can select an absolute Node executable; per-account `node_bin` is also supported. The initializer
copies a supplied TokenHub auth file to ignored local storage and records an available Node path.
Its local `auth_file` supplies Satoken; expired
sessions require updating that file. Windows uses the explicit ANSI certificate path expected by
curl_cffi. ClinePass and DeepSeek use fixed HTTPS API origins with redirects disabled.

TeleAgent reads the official portal's `/points/detail` and `/usage/summary` GET endpoints under
`https://agent.teleai.com.cn/meter/super-agent/user/portal`. Today's date is computed in UTC+8;
no client/model filter is sent. Save the signed-in portal's `operationToken` cookie, URL-decoded,
as `{"operation_token":"YOUR_SESSION"}` in ignored `teleagent-auth.local.json`, then run:

```powershell
python collect.py add teleagent --id teleagent-main --name teleagent --account-id teleagent-personal --auth-file teleagent-auth.local.json
```

If the row already exists, enable it instead. Restart after changing configuration. Credentials
stay on the collector; expired sessions require renewal. Both reads must succeed and all three
metrics must be present before replacing a successful snapshot. Missing metrics never become
zero. Point groups/expiry and task/call counts remain in `details.json`; the portal notes about
30 minutes of delay for usage details. No check-in, reward claiming or spending action is called.
Across computers, explicitly share `account_id` for the same account; different accounts need
different identities. Login-token rotation does not define account identity.

## Multiple computers and account identity

Each computer publishes `<prefix>/<collector_id>/latest.json` and `details.json`; no computer
writes a shared index or another computer's objects. `collector_id: auto` persists a random ID
with a local machine fingerprint. Copying configuration/state to another machine produces a new
ID automatically. Explicit IDs are supported but must be unique. A local file lock prevents two
processes using the same state directory. Independent processes must not share an explicit ID.

Firmware discovers directories with COS ListBucket pagination. Records are keyed by provider and
stable `account_id`; the newest **source** timestamp wins across machines, with successful data
preferred on equal timestamps. Upload order is irrelevant. Different accounts stay separate even
if their display names match. Selection follows stable identity when rows reorder or a winning
machine changes. ChatGPT derives a hash from the account/workspace identity. API providers derive
a hash from their API key; supply the same explicit `account_id` to merge different keys for one
account. TokenHub needs a shared explicit `account_id` (or the same local account `id`) on all PCs.
Never reuse an explicit account ID for different people/accounts.

A successful report replaces only its own collector's rows. Deleted/disabled accounts disappear
on the next publication. HTTP 404 or a complete directory scan removes absent collectors.
Stopped computers retain their last data and become stale. Collection failure retains the previous
source time and values with an error flag. Credential/configuration changes invalidate local cached
account data. Upload failure preserves successful local files and retries on the next cycle. Readers
can still display another computer's more recent observation.

## Data contract and extensions

`schema_version: 2` reports contain `collector_id`, UTC `collected_at`, and `accounts`. Each row has
`local_id`, `provider`, stable `account_id`, printable ASCII `name` and `label`, `status`, `checked_at`
and nullable source `collected_at`. Display fields include `plan`, up to three `windows`, up to two
`balances`, optional `tokenhub` amounts, `reset_credits`, or `points`. Window percentages, duration and reset
epoch are validated. Money stays decimal strings; currencies are not combined. TokenHub remaining
is explicitly an estimate of allocation minus monthly consumption, including unlimited allocation
and negative estimated remainder. Failure never invents a new source timestamp or zero usage.

`details.json` retains all business fields returned by enabled adapters, including unfamiliar fields,
all rate-limit buckets, credit details and token usage. Authentication/PII keys and configured secret
values are removed. It is private account usage data. The firmware downloads only `latest.json`,
limited to less than 24 KiB; details are limited to 2 MB. Details are written before the summary;
compare `collected_at` before joining the independently atomic objects. A partially failed upload
may leave different generations; the summary stays usable independently.

To extend, add `collector/plugins/<provider>.py` exporting `LABEL` and
`collect(config, secrets) -> dict`. Return a stable `identity`, optional standard display fields and
`details` containing full business data. Unknown result fields are retained in detail `data`.
Raise `CollectionError(code)` for a safe diagnostic code. Generic windows/balances/points display without
a firmware change; specialized displays can be added in firmware independently. Unknown/missing
provider metrics remain unknown. Empty reports are valid. Firmware supports 32 source records and
32 collector directories per scan; duplicates also occupy source capacity. Capacity overflow is
reported explicitly without silently dropping an existing collector.

The mutually exclusive `points` shape contains nonnegative decimal strings `balance`,
`spent_today` and `added_today` (each at most 23 characters). It cannot accompany windows,
balances, TokenHub amounts or reset credits. It shares the existing balance storage on the
device, preserving the snapshot RAM budget. Additional points plugins need no firmware change.

Legacy schema-1 Codex directory reports remain readable. The old root `tokenhub.json` path is no
longer polled; use the new TokenHub module. Legacy NVS configuration version 3 (including Wi-Fi
and COS) remains compatible. Old provider-key fields are retained only for backup compatibility
and are not used for collection. Cache version 7 discards older layouts and marks boot cache stale.

## ChatGPT reset opportunities and credits

The adapter calls official `account/rateLimits/read` with reset details enabled and optional
`account/usage/read`. It retains all rate-limit groups, workspace credits, reset-credit rows,
`availableCount`, grant/expiry times, optional token summaries and daily buckets. See the
[official app-server protocol](https://learn.chatgpt.com/docs/app-server).

Available reset opportunities are **not a paid recharge count**. Purchase history is not returned
by this protocol, so the collector does not invent that count. `availableCount` is authoritative;
returned detail rows may be capped. Missing counts are null; null detail rows differ from an empty
array. `latest_expires_at_unix` is the expiry of the latest-granted available reset opportunity;
`next_expires_at_unix` is the earliest known expiry. Neither represents the most recent redemption.
Original detail rows remain available for a future firmware interpretation. Collection never calls
reset consumption, billing, model inference, email or other mutation methods.

The ChatGPT page currently shows primary quota windows and the available reset count; its third
row shows the earliest known expiry among available opportunities in Beijing time, using
`next_expires_at_unix`. A zero count shows that no opportunities are available; an unknown
earliest expiry stays unknown and never falls back to the latest-granted opportunity's expiry.
The collector preserves additional fields regardless of the current firmware display.

## Firmware configuration and controls

When `collector/config.local.json` exists, CMake automatically generates ignored
`build/private/quota_private_cos.h` from its COS parameters and secret references. No manual
header step is required. Optional `device_cos` overrides select separate read-only credentials.
Configuration errors fail the build without printing values. COS credentials and optional Wi-Fi defaults are embedded;
provider keys and ChatGPT authentication never enter firmware. If the saved device COS
configuration is incomplete, firmware applies the embedded configuration as a whole; a complete
custom configuration retains priority. To change a complete saved configuration, use the device
setup page. Firmware/ELF containing COS credentials must remain private.

Without a local collector configuration, an existing private header is still supported:

```powershell
python tools/prepare_private_cos.py --config collector/config.local.json
```

When the workspace-parent `key.txt` exists, CMake also reads its `ssid` and `pass` (or `password`)
fields. Only those Wi-Fi fields are imported from that file; provider keys are ignored. SSIDs are
UTF-8, limited to 32 bytes. Passwords may be empty for an open network, 8..63 bytes, or a 64-digit
hexadecimal PSK. Invalid or incomplete Wi-Fi fields fail the build without printing their values.
Fixed-width C escapes preserve Chinese SSIDs and special characters exactly. Set the CMake
`QUOTA_PRIVATE_KEY_FILE` cache option to select another local file. Changes to that file trigger
regeneration. Without Wi-Fi fields, builds retain the existing COS-only behavior.

At startup, a device with no saved Wi-Fi configuration uses the embedded SSID/password and
connects automatically. Saved Wi-Fi takes priority; long-press OK to change it through the setup
page. Defaults stay in the private app image and need no separate filesystem or data partition.
A manual equivalent is:

```powershell
python tools/prepare_private_cos.py --config collector/config.local.json --key-file ../key.txt
```

Without embedded Wi-Fi, fresh firmware needs 2.4 GHz Wi-Fi credentials. Long-press OK to open/close the randomized
WPA2 device hotspot and visit `http://192.168.4.1`. The configuration page is restricted to the
hotspot interface. Wi-Fi changes are tested before persistence; failed changes retain the old
configuration. The page can override COS and export/import local JSON or multipart offline QR
backups. Blank optional form values preserve saved data; explicit clear options remove them.

| Control | Action |
| --- | --- |
| Up/down click | Previous/next account |
| OK click | Queue synchronization, minimum ten seconds between manual requests |
| OK long press | Open/close setup |
| Up long press | Toggle ten-second automatic page rotation |
| Down long press | Reconnect Wi-Fi |
| First key after dimming | Wake backlight |

Automatic sync runs every five minutes; data older than fifteen minutes, future timestamps and
errors are marked stale. Reset countdowns never replenish quota locally. HTTPS verifies TLS,
disables redirects and uses one connection at a time after SNTP synchronization. Device COS
permissions need ListBucket/GetObject for the prefix; uploaders need PutObject on their machine
objects. Collection does not change bucket policies or ACLs. Temporary COS credentials need renewal.

## Validation and delivery

Activate ESP-IDF 5.5.3 and run `bash tools/validate.sh` for repository checks, host tests, firmware
build, merged-layout verification and matching debug archive. Collector tests are offline and use
no credentials. Live provider/COS tests are reported separately. New display text is included in
the generated CJK font inventory. The board is ESP32-C3, 8 MB Flash, no PSRAM, 240 x 320 display.

Use the verified full image at `0x0`; merged flashing can reset NVS. With this application's matching
partition table, the app-only image at `0x20000` can preserve NVS. The app-only image must never be
written at `0x0`. Build/test commands do not flash. On-device page rendering, hotspot interaction,
network stability, memory headroom and physical controls require separate authorized testing.

## Version control

The application and collector sources are maintained together on `main`; use feature branches
for subsequent development. Public history uses synthetic test data. Old local branches are
private working history and must not be included in bulk pushes.
Use small Conventional Commits for subsequent changes and run `bash tools/validate.sh --static`
before committing; firmware changes also require the full validation gate. Shell scripts use
LF line endings on every platform, while Windows launchers use CRLF.

```sh
git status --short --branch
git diff
git add <changed-source-files>
git diff --cached --stat
git diff --cached --check
git commit -m "fix(collector): describe the change"
git log --oneline -5
```

Local configuration, API keys, login sessions, collected account data, dependencies and private
firmware/packages are ignored. Keep example configuration, dependency lockfiles, font licenses,
source assets and tests tracked. Do not force-add ignored private files. Local commits do not
publish or back up the project to a server. `origin` points to the owner's GitHub project;
`upstream` retains the original public repository. Push reviewed project updates with
`git push origin main`; do not use `--all` or `--mirror` to publish local history.
