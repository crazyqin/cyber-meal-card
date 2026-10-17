#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -x "$root/.venv/bin/python" ]]; then
    echo 'Run bash install.sh first.' >&2; exit 1
fi
export PATH="$root/.runtime/node/bin:$root/.runtime/codex:$PATH"
export PYTHONUTF8=1
exec "$root/.venv/bin/python" "$root/deploy.py" "$@"
