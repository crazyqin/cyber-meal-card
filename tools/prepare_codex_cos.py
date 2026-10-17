#!/usr/bin/env python3
"""Prepare a private, local device-setup import; never embed keys in firmware."""
import argparse
import json
import os
from pathlib import Path


def prepare(directory):
    config = json.loads((directory / 'config.json').read_text(encoding='utf-8-sig'))
    secrets = json.loads((directory / 'secrets.json').read_text(encoding='utf-8-sig'))
    cos = config['cos']
    result = {f'cos_{k}': cos[k] for k in ('bucket', 'region', 'prefix')}
    for source, target in [('COS_SECRET_ID', 'cos_secret_id'), ('COS_SECRET_KEY', 'cos_secret_key'), ('COS_SESSION_TOKEN', 'cos_token')]:
        result[target] = os.environ.get(source) or secrets.get(source) or ''
    if not result['cos_secret_id'] or not result['cos_secret_key']:
        raise ValueError('COS credentials are missing')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--collector-dir', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=Path('build/codex-cos-auto-config.json'))
    args = parser.parse_args()
    try:
        result = prepare(args.collector_dir)
        args.output.parent.mkdir(parents=True, exist_ok=True)
        # Refuse to accidentally overwrite an existing unrelated file.
        fd = os.open(args.output, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(fd, 'w', encoding='utf-8') as stream:
            json.dump(result, stream, ensure_ascii=False, indent=2)
            stream.write('\n')
    except (OSError, ValueError, KeyError, TypeError):
        parser.exit(1, 'Could not prepare device configuration. Check inputs and use a new output path.\n')
    print(f'Prepared private import for automatic account discovery: {args.output.resolve()}')


if __name__ == '__main__':
    main()
