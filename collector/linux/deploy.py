"""Linux deployment controls. This file lives beside app/ in the deployment bundle."""
from __future__ import annotations
from contextlib import contextmanager
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent
APP = ROOT / 'app'
CONFIG = ROOT / 'config.local.json'
STATE = ROOT / 'state'


def helpers():
    sys.path.insert(0, str(APP))
    import collect
    return collect


def initialize():
    collect = helpers()
    from common import atomic_json, read_json
    for name in ('state', 'settings', 'accounts'):
        (ROOT / name).mkdir(exist_ok=True)
    secrets_path = ROOT / 'secrets.local.json'
    if CONFIG.exists():
        collect.load_config(CONFIG)
        if not secrets_path.exists():
            atomic_json(secrets_path, {})
        return
    config = read_json(APP / 'config.example.json')
    config.update(collector_id='auto', state_dir='state', secrets_file='secrets.local.json')
    preset = ROOT / 'settings/cos.local.json'
    secrets = read_json(secrets_path) if secrets_path.exists() else {}
    if preset.exists():
        private = read_json(preset)
        config['cos'] = private['cos']
        for ref, value in private['secrets'].items():
            secrets.setdefault(ref, value)
    for row in config['accounts']:
        if row['provider'] == 'chatgpt':
            row.update(codex_home='accounts/' + row['id'], codex_bin=str(ROOT / '.runtime/codex/codex'))
        elif row['provider'] == 'tokenhub':
            row.update(auth_file='settings/tokenhub-auth.local.json', node_bin=str(ROOT / '.runtime/node/bin/node'))
        elif row['provider'] == 'teleagent':
            row['auth_file'] = 'settings/teleagent-auth.local.json'
    atomic_json(secrets_path, secrets)
    atomic_json(CONFIG, config)
    print('本机配置已初始化；机器 ID 将自动生成。COS 凭据未显示。')


def config():
    initialize()
    return helpers().load_config(CONFIG)


def command():
    return [sys.executable, str(APP / 'collect.py'), '--config', str(CONFIG), 'run']


def process_identity(pid):
    """Bind control to this exact process, rejecting stale/reused PIDs."""
    try:
        stat = Path(f'/proc/{pid}/stat').read_text()
        fields = stat.rsplit(')', 1)[1].split()
        args = Path(f'/proc/{pid}/cmdline').read_bytes().split(b'\0')
        expected = {str(APP / 'collect.py').encode(), str(CONFIG).encode(), b'run'}
        if fields[0] == 'Z' or not expected.issubset(set(args)):
            return None
        return fields[19]
    except (OSError, IndexError, ValueError):
        return None


def running():
    from common import read_json
    try:
        record = read_json(STATE / 'process.json')
        pid = record['pid']
        if not isinstance(pid, int) or pid <= 1:
            return None
        identity = process_identity(pid)
        return pid if identity is not None and identity == record.get('start_ticks') else None
    except (OSError, ValueError, KeyError, TypeError):
        return None


@contextmanager
def control_lock():
    import fcntl
    with (STATE / '.control.lock').open('a+b') as handle:
        fcntl.flock(handle.fileno(), fcntl.LOCK_EX)
        yield


def start():
    config()
    from common import atomic_json
    with control_lock():
        if running():
            print('采集器已经运行。'); return 0
        with (STATE / 'collector.log').open('ab', buffering=0) as log:
            child = subprocess.Popen(command(), cwd=ROOT, stdin=subprocess.DEVNULL,
                                     stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        time.sleep(1)
        identity = process_identity(child.pid)
        if child.poll() is not None or identity is None:
            print('启动失败，请运行 bash collector.sh logs。', file=sys.stderr); return 1
        atomic_json(STATE / 'process.json', {'pid':child.pid, 'start_ticks':identity})
        print('已在后台启动，每 5 分钟采集一次。用 status/check 查看结果。')
        return 0


def stop():
    with control_lock():
        pid = running()
        if pid is None:
            print('没有本部署目录管理的运行进程。'); return 0
        if os.getpgid(pid) != pid:
            raise ValueError('Unexpected process group')
        os.killpg(pid, signal.SIGTERM)
        for _ in range(100):
            if running() is None:
                (STATE / 'process.json').unlink(missing_ok=True)
                print('已停止本机采集器。'); return 0
            time.sleep(0.1)
        print('进程尚未退出，请查看日志。', file=sys.stderr)
        return 1


def login(account='chatgpt-main'):
    loaded = config()
    row = next((r for r in loaded['accounts'] if r['id'] == account and r['provider'] == 'chatgpt'), None)
    if row is None:
        raise ValueError('Unknown ChatGPT account')
    home = Path(row['codex_home']); home.mkdir(parents=True, exist_ok=True)
    auth_config = home / 'config.toml'
    if not auth_config.exists():
        auth_config.write_text('cli_auth_credentials_store = "file"\n')
    env = os.environ.copy(); env['CODEX_HOME'] = str(home)
    return subprocess.call([row.get('codex_bin',str(ROOT/'.runtime/codex/codex')), 'login', '--device-auth'], env=env, cwd=ROOT)


def tokenhub_auth():
    import getpass
    from common import atomic_json
    loaded = config()
    row = next((r for r in loaded['accounts'] if r['provider'] == 'tokenhub'), None)
    if row is None:
        raise ValueError('TokenHub module missing')
    token = getpass.getpass('输入该账号 Satoken（不回显）: ').strip()
    if not token or any(c in token for c in '\r\n'):
        raise ValueError('Invalid token')
    atomic_json(Path(row['auth_file']), {'satoken':token, 'gray_flag':'0'})
    print('TokenHub 凭据已保存；请用 configure 启用模块，重启后生效。')


def runtime_check():
    import requests
    import qcloud_cos
    import curl_cffi
    loaded = config()
    subprocess.run([str(ROOT/'.runtime/node/bin/node'), '--version'], check=True)
    subprocess.run([str(ROOT/'.runtime/codex/codex'), '--version'], check=True)
    subprocess.run([str(ROOT/'.runtime/node/bin/node'), '-e',
                    "require('@napi-rs/canvas'); require('sdenv-jsdom'); console.log('TokenHub runtime: PASS')"],
                   cwd=APP/'tokenhub_runtime/waf_runtime', check=True)
    print('依赖检查通过。ChatGPT 仍需登录；平台实采用 check 验证。')


def teleagent_auth():
    import getpass
    from common import atomic_json
    loaded = config()
    row = next((r for r in loaded['accounts'] if r['provider'] == 'teleagent'), None)
    if row is None:
        raise ValueError('TeleAgent module missing; add it with cli add first')
    token = getpass.getpass('输入星辰门户的 operationToken（不回显）: ').strip()
    if not token or any(ord(c) < 32 or ord(c) > 126 for c in token):
        raise ValueError('Invalid token')
    atomic_json(Path(row['auth_file']), {'operation_token': token})
    print('星辰门户凭据已保存；请用 configure 启用模块，重启后生效。')


def main():
    os.umask(0o077)
    STATE.mkdir(parents=True, exist_ok=True)
    action = sys.argv[1] if len(sys.argv)>1 else 'status'
    try:
        initialize()
        if action == 'init': return 0
        if action == 'runtime-check': runtime_check(); return 0
        if action == 'login': return login(sys.argv[2] if len(sys.argv)>2 else 'chatgpt-main')
        if action == 'configure': helpers().configure(CONFIG); return 0
        if action == 'tokenhub-auth': tokenhub_auth(); return 0
        if action == 'teleagent-auth': teleagent_auth(); return 0
        if action == 'start': return start()
        if action == 'stop': return stop()
        if action == 'restart':
            result=stop()
            return result if result else start()
        if action in ('run', 'check'):
            args=command()
            if action == 'check': args.append('--once')
            return subprocess.call(args,cwd=ROOT)
        if action == 'cli':
            return subprocess.call([sys.executable,str(APP/'collect.py'),'--config',str(CONFIG),*sys.argv[2:]],cwd=ROOT)
        if action == 'status':
            print('运行中' if running() else '未通过 start 启动')
            path=STATE/'status.json'
            if path.exists(): print(path.read_text(encoding='utf-8'))
            else: print('尚无采集结果。')
            return 0
        if action == 'logs':
            path=STATE/'collector.log'
            print('\n'.join(path.read_text(encoding='utf-8',errors='replace').splitlines()[-100:]) if path.exists() else '暂无日志。')
            return 0
        print('Commands: init/login/configure/tokenhub-auth/teleagent-auth/check/start/stop/restart/status/logs/run/cli')
        return 2
    except KeyboardInterrupt:
        return 130
    except Exception:
        print('操作未完成，请检查配置、依赖和日志。凭据不会显示。',file=sys.stderr)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
