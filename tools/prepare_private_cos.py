#!/usr/bin/env python3
"""Generate private COS and optional Wi-Fi defaults without printing credentials."""
import argparse
import json
import os
import re
from pathlib import Path
from prepare_codex_cos import prepare


def prepare_config(path):
    config = json.loads(path.read_text(encoding='utf-8-sig'))
    secrets_path = Path(config.get('secrets_file', 'secrets.local.json'))
    if not secrets_path.is_absolute():
        secrets_path = path.parent / secrets_path
    secrets = json.loads(secrets_path.read_text(encoding='utf-8-sig'))
    cos = {**config['cos'], **config.get('device_cos', {})}
    values = {f'cos_{k}': cos[k] for k in ('bucket', 'region', 'prefix')}
    for source, target in [('secret_id_ref', 'cos_secret_id'), ('secret_key_ref', 'cos_secret_key'), ('token_ref', 'cos_token')]:
        ref = cos.get(source, '')
        values[target] = os.environ.get(ref) or secrets.get(ref) or ''
    if not values['cos_secret_id'] or not values['cos_secret_key']:
        raise ValueError('COS credentials missing')
    if not re.fullmatch(r'[a-z0-9-]+-\d+', values['cos_bucket']) or not re.fullmatch(r'[a-z0-9-]+', values['cos_region']):
        raise ValueError('Invalid COS bucket/region')
    if not values['cos_prefix'] or len(values['cos_prefix']) > 128 or any(
        not re.fullmatch(r'[A-Za-z0-9_.~-]+', p) or '..' in p for p in values['cos_prefix'].split('/')):
        raise ValueError('Invalid COS prefix')
    return values


def prepare_wifi(path):
    aliases = {'ssid':'ssid', 'wifi_ssid':'ssid', 'wifi_name':'ssid', 'wifi名称':'ssid',
               '网络名称':'ssid', 'pass':'password', 'password':'password', 'pwd':'password',
               'wifi_password':'password', 'wifi密码':'password'}
    values = {}
    for line in path.read_text(encoding='utf-8-sig').splitlines():
        parts = re.split(r'[:：=]', line, maxsplit=1)
        if len(parts) != 2:
            continue
        field = aliases.get(parts[0].strip().lower())
        if field is None:
            continue
        if field in values:
            raise ValueError('Duplicate Wi-Fi field')
        value = parts[1].strip()
        # Quoted values allow an SSID/passphrase with intentional outer spaces.
        values[field] = json.loads(value) if value.startswith('"') else value
    if not values:
        return {}
    if set(values) != {'ssid','password'} or not all(isinstance(v,str) for v in values.values()):
        raise ValueError('Incomplete Wi-Fi fields')
    if not 1 <= len(values['ssid'].encode('utf-8')) <= 32:
        raise ValueError('Invalid Wi-Fi SSID length')
    password = values['password'].encode('utf-8')
    if password and not (8 <= len(password) <= 63 or
                         (len(password) == 64 and re.fullmatch(rb'[0-9A-Fa-f]{64}',password))):
        raise ValueError('Invalid Wi-Fi password length')
    return values


def c_literal(value):
    # Fixed-width octal escapes retain exact UTF-8 bytes, quotes, backslashes and
    # question marks without C universal-character or trigraph ambiguities.
    return '"' + ''.join(chr(b) if 32 <= b < 127 and chr(b) not in '"\\?' else f'\\{b:03o}'
                         for b in value.encode('utf-8')) + '"'


def generate(values, target):
    capacities = {'cos_bucket':96, 'cos_region':48, 'cos_prefix':129,
                  'cos_secret_id':128, 'cos_secret_key':128, 'cos_token':2048,
                  'ssid':33, 'password':65}
    for key,value in values.items():
        if (not isinstance(value,str) or (key.startswith('cos_') and not value.isascii()) or
            any(ord(c)<32 or ord(c)==127 for c in value) or len(value.encode('utf-8')) >= capacities[key]):
            raise ValueError('Invalid private COS input')
    text='#pragma once\nstatic const quota_config_t quota_private_cos_defaults = {\n'
    text+=''.join('    .'+key+' = '+c_literal(value)+',\n' for key,value in values.items())+'};\n'
    target.parent.mkdir(parents=True,exist_ok=True)
    if not target.exists() or target.read_text(encoding='utf-8') != text:
        target.write_text(text,encoding='utf-8')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    sources=parser.add_mutually_exclusive_group(required=True)
    sources.add_argument('--collector-dir',type=Path)
    sources.add_argument('--config',type=Path)
    parser.add_argument('--key-file',type=Path,help='Optional local text file with ssid/pass fields')
    args=parser.parse_args()
    target=Path(__file__).resolve().parents[1]/'build/private/quota_private_cos.h'
    try:
        values=prepare_config(args.config.resolve()) if args.config else prepare(args.collector_dir)
        if args.key_file:
            values.update(prepare_wifi(args.key_file))
        generate(values,target)
    except (OSError,ValueError,KeyError,TypeError):
        parser.exit(1,'Could not prepare private defaults. Check local COS inputs.\n')
    print('Private device defaults prepared under ignored build/private (values hidden).')

if __name__=='__main__':main()
