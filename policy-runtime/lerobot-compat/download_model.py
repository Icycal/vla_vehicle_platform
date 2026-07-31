#!/usr/bin/env python3

import argparse
import hashlib
import json
import os
from datetime import datetime, timezone
from pathlib import Path

from huggingface_hub import HfApi, snapshot_download


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo-id', default='lerobot/smolvla_base')
    parser.add_argument('--revision', default='main')
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--max-workers', type=int, default=2)
    arguments = parser.parse_args()

    output_dir = arguments.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    token = os.environ.get('HF_TOKEN') or None
    revision = HfApi().model_info(
        arguments.repo_id,
        revision=arguments.revision,
        token=token,
    ).sha
    print('repo_id:', arguments.repo_id)
    print('resolved_revision:', revision)
    snapshot_download(
        repo_id=arguments.repo_id,
        revision=revision,
        local_dir=output_dir,
        token=token,
        max_workers=arguments.max_workers,
    )

    files = []
    for path in sorted(output_dir.rglob('*')):
        relative = path.relative_to(output_dir)
        if not path.is_file() or '.cache' in relative.parts:
            continue
        if relative.as_posix() == 'model-manifest.json':
            continue
        files.append({
            'path': relative.as_posix(),
            'size': path.stat().st_size,
            'sha256': sha256_file(path),
        })

    manifest = {
        'schema_version': 1,
        'repo_id': arguments.repo_id,
        'requested_revision': arguments.revision,
        'resolved_revision': revision,
        'downloaded_at_utc': datetime.now(timezone.utc).isoformat(),
        'files': files,
        'total_size': sum(item['size'] for item in files),
    }
    manifest_path = output_dir / 'model-manifest.json'
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print('file_count:', len(files))
    print('total_size_bytes:', manifest['total_size'])
    print('SMOLVLA_MODEL_DOWNLOAD_PASS')


if __name__ == '__main__':
    main()
