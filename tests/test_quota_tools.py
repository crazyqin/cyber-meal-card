"""Validate the application font inventory without opening an account."""
from pathlib import Path
import re
import unittest
import importlib.util
import json
import tempfile
import sys
import ast
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
class QuotaToolsTests(unittest.TestCase):
    def test_private_wifi_import_limits_and_exact_utf8_c_literals(self):
        sys.path.insert(0,str(ROOT/'tools'))
        import prepare_private_cos as module
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'key.txt'
            path.write_text('clinepass:not-for-firmware\nssid:家庭网络\npass:abc123456\n',encoding='utf-8')
            values=module.prepare_wifi(path)
            self.assertEqual(values,{'ssid':'家庭网络','password':'abc123456'})
            self.assertNotIn('cline',values)
            for text in ('家庭网络','name"with\\slash??7',' space at ends '):
                self.assertEqual(ast.literal_eval('b'+module.c_literal(text)),text.encode('utf-8'))
            path.write_text('ssid:'+('家'*10)+'ab\npass:'+('a'*64)+'\n',encoding='utf-8')
            self.assertEqual(len(module.prepare_wifi(path)['ssid'].encode()),32)
            for text in ('ssid:'+('家'*11)+'\npass:abcdefgh\n', 'ssid:only-one-field\n',
                         'ssid:network\npass:short\n','ssid:network\npass:'+('z'*64)+'\n',
                         'ssid:network\nssid:duplicate\npass:abcdefgh\n'):
                path.write_text(text,encoding='utf-8')
                with self.assertRaises(ValueError): module.prepare_wifi(path)
            path.write_text('ssid:" network "\npass:\n',encoding='utf-8')
            self.assertEqual(module.prepare_wifi(path),{'ssid':' network ','password':''})
            path.write_text('deepseek:ignored-provider-key\n',encoding='utf-8')
            self.assertEqual(module.prepare_wifi(path),{})

    def test_private_firmware_embeds_only_cos_and_supports_device_identity(self):
        sys.path.insert(0, str(ROOT/'tools'))
        import prepare_private_cos
        with tempfile.TemporaryDirectory() as folder, patch.dict('os.environ', {}, clear=True):
            directory=Path(folder)
            config={'cos':{'bucket':'example-1250000000','region':'ap-shanghai','prefix':'usage',
                          'secret_id_ref':'UPLOAD_ID','secret_key_ref':'UPLOAD_KEY'},
                    'device_cos':{'secret_id_ref':'READ_ID','secret_key_ref':'READ_KEY'}}
            (directory/'config.json').write_text(json.dumps(config))
            (directory/'secrets.local.json').write_text(json.dumps({'READ_ID':'READONLYEXAMPLE','READ_KEY':'read-only-value',
                'UPLOAD_ID':'UPLOADEXAMPLE','UPLOAD_KEY':'upload-only-value','CLINE_API_KEY':'no-provider-key-in-firmware'}))
            values=prepare_private_cos.prepare_config(directory/'config.json')
            target=directory/'private.h';prepare_private_cos.generate(values,target)
            source=target.read_text()
            self.assertIn('READONLYEXAMPLE',source)
            self.assertNotIn('upload-only-value',source)
            self.assertNotIn('no-provider-key-in-firmware',source)
            values['cos_token']='x'*2048
            with self.assertRaises(ValueError): prepare_private_cos.generate(values,target)

    def test_font_covers_every_application_character(self):
        source = (ROOT / 'assets/fonts/quota/quota_font_16.c').read_text(encoding='utf-8')
        inventory = (ROOT / 'assets/fonts/quota/inventory.txt').read_text(encoding='utf-8')
        available = {int(value, 16) for value in re.findall(r'^U\+([0-9A-F]+)', inventory, re.M)}
        for path in (ROOT / 'main').glob('quota_*'):
            if path.suffix in ('.c', '.h'):
                required = {ord(c) for c in path.read_text(encoding='utf-8') if ord(c) > 127}
                self.assertFalse(required - available, path.name)
        self.assertIn('.bitmap_format=0', source)
        self.assertIn('LV_FONT_FMT_TXT_CMAP_SPARSE_TINY', source)
        self.assertEqual(len(available), len(set(available)))

    def test_cos_import_has_no_account_selectors_or_login_paths(self):
        spec = importlib.util.spec_from_file_location('prepare_cos', ROOT / 'tools/prepare_codex_cos.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder, patch.dict('os.environ', {}, clear=True):
            directory = Path(folder)
            (directory / 'config.json').write_text(json.dumps({'cos': {'bucket': 'example-1250000000', 'region': 'ap-shanghai', 'prefix': 'codex-usage'}, 'accounts': [{'name': 'ignored', 'codex_home': 'private-login-path'}]}), encoding='utf-8')
            (directory / 'secrets.json').write_text(json.dumps({'COS_SECRET_ID': 'example-id', 'COS_SECRET_KEY': 'example-key'}), encoding='utf-8')
            result = module.prepare(directory)
            self.assertEqual(result['cos_secret_key'], 'example-key')
            self.assertEqual(result['cos_token'], '')
            self.assertEqual(set(result), {'cos_bucket', 'cos_region', 'cos_prefix', 'cos_secret_id', 'cos_secret_key', 'cos_token'})


if __name__ == '__main__':
    unittest.main()
