"""Synthetic downloaded-script isolation checks. No login or external HTTP."""
import json
import os
from pathlib import Path
import shutil
import socket
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'collector'))
from tokenhub_runtime import sandbox
from tokenhub_runtime.http_protocol import JavaScriptWorker


class SandboxTests(unittest.TestCase):
    def test_missing_linux_isolation_fails_closed(self):
        with patch.object(sandbox,'os',SimpleNamespace(name='posix',environ={})),patch.object(sandbox.sys,'platform','linux'), \
             patch.object(sandbox.shutil,'which',return_value=None),self.assertRaises(sandbox.SandboxUnavailable):
            sandbox.worker_process('/example/node',ROOT/'collector/tokenhub_runtime/waf_runtime/worker.cjs')

    @unittest.skipUnless(os.environ.get('TOKENHUB_SANDBOX_NODE'), 'set TOKENHUB_SANDBOX_NODE for OS integration')
    def test_downloaded_script_cannot_read_host_file_spawn_or_connect(self):
        with tempfile.TemporaryDirectory() as folder:
            marker=Path(folder)/'synthetic-private.txt'
            marker.write_text('synthetic-marker')
            script = """(() => {
              const p=XMLHttpRequest.prototype.open.constructor('return process')();
              const req=p.mainModule.require.bind(p.mainModule);
              let read=false, child=false, network=false;
              try { read=req('node:fs').readFileSync(MARKER,'utf8')==='synthetic-marker'; } catch {}
              try { child=req('node:child_process').spawnSync(p.execPath,['--version']).status===0; } catch {}
              try { req('node:net').connect(9,'127.0.0.1'); network=true; } catch {}
              p.stdout.write(JSON.stringify({event:'isolation',read,child,network,env:!!p.env.TEST_PRIVATE_VALUE})+'\\n');
            })();""".replace('MARKER',json.dumps(str(marker)))
            message={'event':'start','mode':'bootstrap','html':'<html><script src="/probe.js"></script></html>',
                     'documentUrl':'https://token.telecomjs.com/finance/cost','userAgent':'OfflineTest',
                     'scripts':{'https://token.telecomjs.com/probe.js':script},'cookies':[],'timeoutMs':4000}
            with patch.dict(os.environ,{'TEST_PRIVATE_VALUE':'synthetic-secret'}), \
                 JavaScriptWorker(10,os.environ['TOKENHUB_SANDBOX_NODE']) as worker:
                worker.send(message)
                result=worker.receive('isolation')
                self.assertEqual(result,{'event':'isolation','read':False,'child':False,'network':False,'env':False})
                self.assertEqual(worker.receive('cookies')['cookies'],[])

    @unittest.skipUnless(os.name == 'nt' and os.environ.get('TOKENHUB_SANDBOX_NODE'), 'Windows OS boundary integration')
    def test_appcontainer_blocks_files_and_tcp_without_node_permission_flags(self):
        with tempfile.TemporaryDirectory() as folder, socket.socket() as server:
            marker=Path(folder)/'os-private.txt'
            marker.write_text('synthetic-marker')
            server.bind(('127.0.0.1',0)); server.listen()
            script="""let read=false;try {read=require('node:fs').readFileSync(MARKER,'utf8')==='synthetic-marker'}catch{}
              const s=require('node:net').connect(PORT,'127.0.0.1');
              const done=network=>{console.log(JSON.stringify({read,network}));s.destroy()};
              s.on('connect',()=>done(true));s.on('error',()=>done(false));setTimeout(()=>done(false),1500).unref();"""
            script=script.replace('MARKER',json.dumps(str(marker))).replace('PORT',str(server.getsockname()[1]))
            node=Path(os.environ['TOKENHUB_SANDBOX_NODE']).resolve()
            env={k:os.environ[k] for k in ('SystemRoot','WINDIR','SystemDrive','USERPROFILE','LOCALAPPDATA','APPDATA','TEMP','TMP') if k in os.environ}
            p=sandbox._windows([str(node),'--preserve-symlinks','--preserve-symlinks-main','--eval',script],env,
                               ROOT/'collector/tokenhub_runtime/waf_runtime',node)
            try:
                self.assertEqual(json.loads(p.stdout.readline()),{'read':False,'network':False})
                self.assertEqual(p.wait(5),0)
            finally:
                p.terminate();p.stdin.close();p.stdout.close();p.release()


if __name__=='__main__':unittest.main()
