#!/usr/bin/env python3
"""Create a Linux native deployment tarball; private COS inclusion is explicit."""
from __future__ import annotations
import argparse
import hashlib
import io
import json
from pathlib import Path
import re
import tarfile

ROOT = Path(__file__).resolve().parents[1]
COLLECTOR = ROOT / 'collector'
PREFIX = 'ai-collector-linux'


def source_files():
    """Explicit source scope, never the running collector's complete directory."""
    files = {}
    for name in ('collect.py', 'common.py', 'summary.py', 'supervise.py', 'requirements.txt', 'config.example.json'):
        files['app/' + name] = COLLECTOR / name
    for folder in ('plugins', 'tokenhub_runtime'):
        for path in (COLLECTOR/folder).rglob('*'):
            if not path.is_file() or path.is_symlink():
                continue
            parts = path.relative_to(COLLECTOR).parts
            if any(p in ('node_modules', '__pycache__', '.venv') for p in parts):
                continue
            if path.suffix not in ('.py', '.js', '.cjs', '.json', '.md') and path.name != 'LICENSE':
                continue
            if path.name.endswith('.local.json') or path.name.startswith('.'):
                continue
            files['app/' + path.relative_to(COLLECTOR).as_posix()] = path
    for name in ('install.sh', 'collector.sh', 'deploy.py', 'runtimes.sh', 'runtime-lock.json', 'README.md', 'README.zh_CN.md'):
        files[name] = COLLECTOR/'linux'/name
    files['LICENSE'] = ROOT/'LICENSE'
    return files


def private_cos(path):
    """Whitelist COS references only; do not copy provider keys or auth files."""
    import os
    config = json.loads(path.read_text(encoding='utf-8-sig'))
    secrets_path = Path(config.get('secrets_file', 'secrets.local.json'))
    if not secrets_path.is_absolute(): secrets_path=path.parent/secrets_path
    secrets = json.loads(secrets_path.read_text(encoding='utf-8-sig'))
    original = config['cos']
    cos = {key:original[key] for key in ('bucket','region','prefix')}
    refs = {'secret_id_ref':'COS_SECRET_ID', 'secret_key_ref':'COS_SECRET_KEY', 'token_ref':'COS_SESSION_TOKEN'}
    credentials = {}
    for field, ref in refs.items():
        cos[field]=ref
        source=original.get(field,'')
        credentials[ref]=os.environ.get(source) or secrets.get(source) or ''
    if not credentials['COS_SECRET_ID'] or not credentials['COS_SECRET_KEY']:
        raise ValueError('COS credentials missing')
    return {'cos':cos,'secrets':credentials}


def build(output, cos_config=None):
    if output.exists(): raise ValueError('Output exists; use a new filename')
    output.parent.mkdir(parents=True,exist_ok=True)
    payload = {name:path.read_bytes() for name,path in source_files().items()}
    for name in tuple(payload):
        if name.endswith(('.sh','.py','.md','.json','.cjs','.js')):
            payload[name]=payload[name].replace(b'\r\n',b'\n')
    if cos_config:
        payload['settings/cos.local.json']=(json.dumps(private_cos(cos_config),indent=2)+'\n').encode()
    payload['部署说明.txt']=payload['README.zh_CN.md']
    manifest = {'schema_version':1,'private_cos_included':bool(cos_config),
                'files':{name:hashlib.sha256(data).hexdigest() for name,data in sorted(payload.items())}}
    payload['manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    with tarfile.open(output,'w:gz',format=tarfile.PAX_FORMAT) as archive:
        for name,data in sorted(payload.items()):
            entry=tarfile.TarInfo(PREFIX+'/'+name)
            entry.size=len(data)
            entry.mode=0o755 if name.endswith('.sh') else 0o600
            entry.mtime=0
            archive.addfile(entry,io.BytesIO(data))
    return verify(output)


def verify(path):
    with tarfile.open(path,'r:gz') as archive:
        names=archive.getnames()
        if any(not member.isfile() for member in archive.getmembers()): raise ValueError('Unexpected archive entry type')
        if len(names)!=len(set(names)): raise ValueError('Duplicate archive paths')
        if any(not name.startswith(PREFIX+'/') or '..' in Path(name).parts for name in names): raise ValueError('Invalid archive path')
        content={name[len(PREFIX)+1:]:archive.extractfile(name).read() for name in names if archive.getmember(name).isfile()}
    manifest=json.loads(content.pop('manifest.json'))
    if set(manifest['files'])!=set(content): raise ValueError('Manifest file list mismatch')
    for name,data in content.items():
        if hashlib.sha256(data).hexdigest()!=manifest['files'][name]: raise ValueError('Checksum mismatch')
        if any(part in ('state','accounts','node_modules','.venv','__pycache__') for part in Path(name).parts):
            raise ValueError('Machine-local data found')
        if name.endswith('.local.json') and name!='settings/cos.local.json': raise ValueError('Unexpected private file')
    if 'app/tokenhub_runtime/waf_runtime/vendor/sdenv/build/Release/documentAll.js' not in content:
        raise ValueError('TokenHub runtime incomplete')
    if ('settings/cos.local.json' in content)!=manifest['private_cos_included']: raise ValueError('Privacy marker mismatch')
    return {'files':len(content)+1,'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'private_cos_included':manifest['private_cos_included']}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--cos-config',type=Path)
    parser.add_argument('--verify',action='store_true')
    args=parser.parse_args()
    try:
        result=verify(args.output) if args.verify else build(args.output,args.cos_config)
    except (OSError,ValueError,KeyError,TypeError,tarfile.TarError):
        parser.exit(1,'Package could not be prepared or verified. Credential values hidden.\n')
    print(json.dumps(result))


if __name__=='__main__':main()
