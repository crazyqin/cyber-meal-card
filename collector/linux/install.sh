#!/usr/bin/env bash
set -euo pipefail
umask 077
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$root"
if [[ "$(uname -s)" != Linux ]]; then
    echo "This deployment package runs on Linux only." >&2; exit 1
fi
for tool in curl tar sha256sum getconf; do
    command -v "$tool" >/dev/null || { echo "Missing system tool: $tool" >&2; exit 1; }
done
glibc="$(getconf GNU_LIBC_VERSION 2>/dev/null || true)"
if [[ "$glibc" != glibc\ * ]]; then
    echo "A glibc Linux distribution is required (for example Ubuntu/Debian)." >&2; exit 1
fi
if ! printf '%s\n%s\n' 2.28 "${glibc#glibc }" | sort -V -C; then
    echo "glibc 2.28 or newer is required." >&2; exit 1
fi
case "$(uname -m)" in
    x86_64|amd64) arch=x86_64 ;;
    aarch64|arm64) arch=aarch64 ;;
    *) echo "Supported architectures: x86_64 and aarch64." >&2; exit 1 ;;
esac
source "$root/runtimes.sh"
if ! command -v bwrap >/dev/null 2>&1; then
    echo 'TokenHub requires bubblewrap and unprivileged user namespaces. Ubuntu/Debian: sudo apt-get install bubblewrap' >&2
    echo 'Other collectors remain usable; TokenHub will fail closed until its sandbox is available.' >&2
fi
mkdir -p .runtime .downloads state accounts settings
download() {
    local url="$1" digest="$2" destination="$3"
    if [[ ! -f "$destination" ]] || ! printf '%s  %s\n' "$digest" "$destination" | sha256sum -c - >/dev/null 2>&1; then
        curl --fail --location --proto '=https' --proto-redir '=https' --retry 3 \
            --connect-timeout 20 --max-time 1800 "$url" -o "${destination}.part"
        printf '%s  %s\n' "$digest" "${destination}.part" | sha256sum -c -
        mv -- "${destination}.part" "$destination"
    fi
}
install_runtime() {
    local name="$1" url="$2" digest="$3" strip="$4"
    local target="$root/.runtime/$name"
    if [[ -f "$target/.archive-sha256" ]] && [[ "$(cat "$target/.archive-sha256")" == "$digest" ]]; then return; fi
    if [[ -e "$target" ]]; then echo "Preserving existing runtime: $target. Use a fresh deployment directory." >&2; exit 1; fi
    download "$url" "$digest" "$root/.downloads/${name}-${arch}.tar.gz"
    local staging
    staging="$(mktemp -d "$root/.runtime/extract-${name}.XXXXXX")"
    tar -xzf "$root/.downloads/${name}-${arch}.tar.gz" --strip-components="$strip" -C "$staging"
    printf '%s\n' "$digest" > "$staging/.archive-sha256"
    mv -- "$staging" "$target"
}
if [[ "$arch" == x86_64 ]]; then
    install_runtime python "$PYTHON_X86_64_URL" "$PYTHON_X86_64_SHA256" 1
    install_runtime node "$NODE_X86_64_URL" "$NODE_X86_64_SHA256" 1
    install_runtime codex "$CODEX_X86_64_URL" "$CODEX_X86_64_SHA256" 0
else
    install_runtime python "$PYTHON_AARCH64_URL" "$PYTHON_AARCH64_SHA256" 1
    install_runtime node "$NODE_AARCH64_URL" "$NODE_AARCH64_SHA256" 1
    install_runtime codex "$CODEX_AARCH64_URL" "$CODEX_AARCH64_SHA256" 0
fi
export PATH="$root/.runtime/node/bin:$PATH"
if [[ ! -f .runtime/codex/codex ]]; then
    codex_binary="$(find "$root/.runtime/codex" -maxdepth 2 -type f -name 'codex-*-unknown-linux-musl' -print -quit)"
    [[ -n "$codex_binary" ]] || { echo 'Codex archive has no expected executable.' >&2; exit 1; }
    ln -s -- "${codex_binary#"$root/.runtime/codex/"}" "$root/.runtime/codex/codex"
fi
"$root/.runtime/python/bin/python3" --version
"$root/.runtime/node/bin/node" --version
"$root/.runtime/codex/codex" --version
if [[ ! -x .venv/bin/python ]]; then "$root/.runtime/python/bin/python3" -m venv "$root/.venv"; fi
"$root/.venv/bin/python" -m pip install --disable-pip-version-check -r "$root/app/requirements.txt"
npm ci --prefix "$root/app/tokenhub_runtime/waf_runtime" --omit=dev --no-audit --no-fund
"$root/.venv/bin/python" "$root/deploy.py" init
"$root/.venv/bin/python" "$root/deploy.py" runtime-check
echo 'Installed locally. Next: bash collector.sh login; bash collector.sh check; bash collector.sh start'
