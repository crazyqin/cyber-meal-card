"""Deadline and recovery tests with synthetic child output; no providers or tasks."""
import json
import logging
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import MagicMock, patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'collector'))
import supervise


class StopAfterCycle:
    def __init__(self): self.stopped=False
    def is_set(self): return self.stopped
    def set(self): self.stopped=True
    def wait(self, _): self.stopped=True


class SupervisorTests(unittest.TestCase):
    def tearDown(self):
        logger=logging.getLogger('collector.supervisor')
        for handler in logger.handlers[:]:
            handler.close();logger.removeHandler(handler)

    def exercise(self, root, child):
        config={'_state':root,'interval_seconds':300,'cycle_timeout_seconds':30}
        with patch.object(supervise,'load_config',return_value=config), \
             patch.object(supervise.threading,'Event',StopAfterCycle), \
             patch.object(supervise.signal,'signal'), \
             patch.object(supervise.subprocess,'Popen',return_value=child), \
             patch.object(supervise,'stop_tree') as stop:
            supervise.supervise(root/'config.json')
        self.tearDown()
        return stop

    def test_timeout_keeps_good_snapshot_and_terminates_only_child(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'latest.json').write_text('{"last":"good"}')
            child=MagicMock();child.communicate.side_effect=subprocess.TimeoutExpired(['synthetic'],30)
            stop=self.exercise(root,child)
            stop.assert_called_once_with(child)
            self.assertEqual(json.loads((root/'status.json').read_text())['error_code'],'cycle_timeout')
            self.assertEqual(json.loads((root/'latest.json').read_text()),{'last':'good'})

    def test_logs_only_safe_status_and_creates_state_on_first_start(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'new-state'
            child=MagicMock(returncode=0)
            child.communicate.return_value=('synthetic-private-value\n2030-01-01T00:00:00Z accounts=2 errors=0 upload=ok\n','')
            stop=self.exercise(root,child)
            stop.assert_not_called()
            log=(root/'collector.log').read_text()
            self.assertNotIn('synthetic-private-value',log)
            self.assertIn('accounts=2 errors=0 upload=ok',log)


if __name__=='__main__':unittest.main()
