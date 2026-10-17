"""Run collection cycles with a deadline, bounded logs and automatic recovery."""
from __future__ import annotations
import argparse
import logging
from logging.handlers import RotatingFileHandler
import os
from pathlib import Path
import re
import signal
import subprocess
import sys
import threading
import time

from common import atomic_json, read_json, utc_now
from collect import load_config, run_lock


def stop_tree(child):
    if child.poll() is not None:
        return
    if os.name == 'nt':
        subprocess.run(['taskkill.exe', '/PID', str(child.pid), '/T', '/F'],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       creationflags=subprocess.CREATE_NO_WINDOW)
    else:
        os.killpg(child.pid, signal.SIGKILL)
    child.wait()


def supervise(path):
    loaded = load_config(path)
    state = loaded['_state']
    state.mkdir(parents=True, exist_ok=True)
    logger = logging.getLogger('collector.supervisor')
    logger.setLevel(logging.INFO)
    logger.addHandler(RotatingFileHandler(state / 'collector.log', maxBytes=1_048_576,
                                         backupCount=3, encoding='utf-8'))
    stopping = threading.Event()
    child = None

    def stop(*_):
        stopping.set()
        if child is not None:
            # Do not wait or acquire Popen's wait lock from a signal handler.
            if os.name != 'nt':
                try:
                    os.killpg(child.pid, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            else:
                child.terminate()

    signal.signal(signal.SIGTERM, stop)
    signal.signal(signal.SIGINT, stop)
    with run_lock(state / 'supervisor'):
        while not stopping.is_set():
            started = time.monotonic()
            cycle_error = False
            try:
                loaded = load_config(path)  # Reload edits at the next cycle.
                if loaded['_state'] != state:
                    raise ValueError('Restart required after changing state directory')
                deadline = loaded.get('cycle_timeout_seconds', 180)
                child = subprocess.Popen([sys.executable, '-X', 'utf8', str(Path(__file__).with_name('collect.py')),
                                          '--config', str(path), 'run', '--once'],
                    stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True, encoding='utf-8',
                    start_new_session=os.name != 'nt',
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name == 'nt' else 0)
                try:
                    output, _ = child.communicate(timeout=deadline)
                    for line in output.splitlines():
                        # Only application status lines enter logs, never arbitrary
                        # plugin output, tracebacks, HTTP bodies or credential values.
                        if re.fullmatch(r'[0-9TZ: -]+(?:accounts=\d+ errors=\d+ upload=\w+|error=secrets_unreadable retry=30)', line):
                            logger.info(line)
                    if child.returncode:
                        logger.info('%s cycle_failed exit=%d', utc_now(), child.returncode)
                except subprocess.TimeoutExpired:
                    stop_tree(child)
                    atomic_json(state / 'status.json', {'checked_at':utc_now(),'running':True,
                                'upload':'skipped','error_code':'cycle_timeout'})
                    logger.info('%s error=cycle_timeout', utc_now())
                finally:
                    child.stdout.close()
                    child = None
            except (OSError, ValueError):
                cycle_error = True
                logger.info('%s error=config_or_runtime retry=30', utc_now())
            if stopping.is_set():
                break
            try:
                error = read_json(state / 'status.json').get('error_code')
            except (OSError, ValueError):
                error = 'status_unreadable'
            interval = 30 if error or cycle_error else loaded.get('interval_seconds', 300)
            stopping.wait(max(1, interval - (time.monotonic() - started)))
        atomic_json(state / 'supervisor-status.json', {'checked_at':utc_now(),'running':False})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--config', type=Path, default=Path(__file__).with_name('config.local.json'))
    args = parser.parse_args()
    try:
        supervise(args.config.resolve())
    except Exception:
        print('Supervisor stopped; configuration and values hidden.', file=sys.stderr)
        raise SystemExit(1)
