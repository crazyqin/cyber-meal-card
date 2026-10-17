"""Deployment checks without a Linux VM, Docker, real logins or cloud mutation."""
import importlib.util
import json
from pathlib import Path, PurePosixPath
import sys
import tarfile
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'collector'))
sys.path.insert(0,str(ROOT/'tools'))
import package_collector_linux as pack

spec=importlib.util.spec_from_file_location('linux_deploy',ROOT/'collector/linux/deploy.py')
deploy=importlib.util.module_from_spec(spec);spec.loader.exec_module(deploy)


class LinuxDeploymentTests(unittest.TestCase):
    def test_public_bundle_is_complete_and_excludes_machine_state(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'public.tar.gz'
            result=pack.build(path)
            self.assertFalse(result['private_cos_included'])
            with tarfile.open(path) as archive:
                names=archive.getnames()
                self.assertIn('ai-collector-linux/install.sh',names)
                self.assertEqual(archive.getmember('ai-collector-linux/install.sh').mode,0o755)
                self.assertNotIn('ai-collector-linux/settings/cos.local.json',names)
                self.assertIn('ai-collector-linux/app/plugins/teleagent.py',names)
                self.assertIn('ai-collector-linux/app/summary.py',names)
                self.assertIn('ai-collector-linux/app/supervise.py',names)
                self.assertIn('ai-collector-linux/app/tokenhub_runtime/sandbox.py',names)
                for name in names:
                    self.assertNotIn('/node_modules/',name)
                    self.assertNotIn('/state/',name)
                    self.assertNotIn('/accounts/',name)
                    if name.endswith(('.sh','.py','.js','.cjs')):
                        self.assertNotIn(b'\r\n',archive.extractfile(name).read())

    def test_private_bundle_whitelists_cos_without_provider_keys(self):
        with tempfile.TemporaryDirectory() as folder,patch.dict('os.environ',{},clear=True):
            root=Path(folder)
            config={'cos':{'bucket':'example-1250000000','region':'ap-shanghai','prefix':'usage',
                    'secret_id_ref':'UPLOAD_ID','secret_key_ref':'UPLOAD_KEY'}}
            (root/'config.json').write_text(json.dumps(config))
            (root/'secrets.local.json').write_text(json.dumps({'UPLOAD_ID':'COS-ID-EXAMPLE',
                'UPLOAD_KEY':'cos-secret-example','CLINE_API_KEY':'provider-must-not-transfer','ACCESS_TOKEN':'login-must-not-transfer'}))
            path=root/'private.tar.gz';result=pack.build(path,root/'config.json')
            self.assertTrue(result['private_cos_included'])
            with tarfile.open(path) as archive:
                data={m.name:archive.extractfile(m).read() for m in archive.getmembers()}
                private=json.loads(data['ai-collector-linux/settings/cos.local.json'])
                self.assertEqual(private['secrets']['COS_SECRET_KEY'],'cos-secret-example')
                self.assertEqual(archive.getmember('ai-collector-linux/settings/cos.local.json').mode,0o600)
                joined=b''.join(data.values())
                self.assertNotIn(b'provider-must-not-transfer',joined)
                self.assertNotIn(b'login-must-not-transfer',joined)
            with self.assertRaises(ValueError):pack.build(path)

    def test_init_uses_new_machine_paths_and_preserves_existing_configuration(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            (root/'settings').mkdir()
            preset={'cos':{'bucket':'example-1250000000','region':'ap-shanghai','prefix':'usage'},
                    'secrets':{'COS_SECRET_ID':'example-id','COS_SECRET_KEY':'example-key'}}
            (root/'settings/cos.local.json').write_text(json.dumps(preset))
            with patch.object(deploy,'ROOT',root),patch.object(deploy,'APP',ROOT/'collector'),patch.object(deploy,'CONFIG',root/'config.local.json'):
                deploy.initialize()
                config=json.loads((root/'config.local.json').read_text())
                self.assertEqual(config['collector_id'],'auto')
                account=config['accounts'][0]
                self.assertEqual(account['codex_home'],'accounts/chatgpt-main')
                self.assertEqual(account['codex_bin'],str(root/'.runtime/codex/codex'))
                config['interval_seconds']=600
                (root/'config.local.json').write_text(json.dumps(config))
                deploy.initialize()
                self.assertEqual(json.loads((root/'config.local.json').read_text())['interval_seconds'],600)
                self.assertFalse((root/'state/identity.json').exists())

    def test_teleagent_auth_saved_without_echo(self):
        with tempfile.TemporaryDirectory() as folder:
            auth=Path(folder)/'teleagent-auth.local.json'
            config={'accounts':[{'provider':'teleagent','auth_file':str(auth)}]}
            with patch.object(deploy,'config',return_value=config),patch('getpass.getpass',return_value='private-example-token'),patch('builtins.print') as output:
                deploy.teleagent_auth()
            self.assertEqual(json.loads(auth.read_text()),{'operation_token':'private-example-token'})
            self.assertNotIn('private-example-token',str(output.call_args_list))

    def test_pid_identity_rejects_unrelated_and_zombie_processes(self):
        fields=['S']+['0']*18+['123456']
        stat='42 (python worker) '+' '.join(fields)
        args=b'\0'.join([b'python',str(deploy.APP/'collect.py').encode(),b'--config',str(deploy.CONFIG).encode(),b'run'])
        with patch.object(Path,'read_text',return_value=stat),patch.object(Path,'read_bytes',return_value=args):
            self.assertEqual(deploy.process_identity(42),'123456')
        supervised=b'\0'.join([b'python',str(deploy.APP/'supervise.py').encode(),b'--config',str(deploy.CONFIG).encode()])
        with patch.object(Path,'read_text',return_value=stat),patch.object(Path,'read_bytes',return_value=supervised):
            self.assertEqual(deploy.process_identity(42),'123456')
        with patch.object(Path,'read_text',return_value=stat),patch.object(Path,'read_bytes',return_value=b'python\0other.py'):
            self.assertIsNone(deploy.process_identity(42))
        with patch.object(Path,'read_text',return_value=stat.replace(') S',') Z')),patch.object(Path,'read_bytes',return_value=args):
            self.assertIsNone(deploy.process_identity(42))

    def test_runtime_sources_pinned_for_both_architectures(self):
        lock=json.loads((ROOT/'collector/linux/runtime-lock.json').read_text())
        self.assertEqual(set(lock),{'x86_64','aarch64'})
        shell=(ROOT/'collector/linux/runtimes.sh').read_text()
        for arch,values in lock.items():
            self.assertEqual(set(values),{'python','node','codex'})
            for name,item in values.items():
                self.assertTrue(item['url'].startswith('https://'))
                self.assertRegex(item['sha256'],r'^[0-9a-f]{64}$')
                self.assertIn(item['url'],shell)
                self.assertIn(item['sha256'],shell)

    def test_service_escapes_paths_without_credentials(self):
        with patch.object(deploy,'ROOT',PurePosixPath('/example/a path%name')), \
             patch.object(deploy,'APP',PurePosixPath('/example/a path%name/app')), \
             patch.object(deploy,'CONFIG',PurePosixPath('/example/a path%name/config.local.json')):
            unit=deploy.service_unit()
        self.assertIn('"/example/a path%%name"',unit)
        self.assertIn('Restart=on-failure',unit)
        self.assertIn('KillMode=control-group',unit)
        self.assertNotIn('secrets.local.json',unit)


if __name__=='__main__':unittest.main()
