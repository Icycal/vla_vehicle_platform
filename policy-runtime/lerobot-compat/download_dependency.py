#!/usr/bin/env python3

import argparse
import json
import os
from datetime import datetime, timezone
from pathlib import Path

from huggingface_hub import HfApi, snapshot_download


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo-id', required=True)
    parser.add_argument('--revision', default='main')
    parser.add_argument('--cache-dir', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--max-workers', type=int, default=2)
    arguments = parser.parse_args()
    token = os.environ.get('HF_TOKEN') or None
    revision = HfApi().model_info(
        arguments.repo_id,
        revision=arguments.revision,
        token=token,
    ).sha
    snapshot_path = snapshot_download(
        repo_id=arguments.repo_id,
        revision=revision,
        cache_dir=arguments.cache_dir,
        token=token,
        max_workers=arguments.max_workers,
        ignore_patterns=['onnx/**', '*.md', '.gitattributes'],
    )
    manifest = {
        'repo_id': arguments.repo_id,
        'requested_revision': arguments.revision,
        'resolved_revision': revision,
        'snapshot_path': str(snapshot_path),
        'downloaded_at_utc': datetime.now(timezone.utc).isoformat(),
    }
    arguments.manifest.parent.mkdir(parents=True, exist_ok=True)
    arguments.manifest.write_text(json.dumps(manifest, indent=2) + '\n')
    print('resolved_revision:', revision)
    print('snapshot_path:', snapshot_path)
    print('SMOLVLA_DEPENDENCY_DOWNLOAD_PASS')


if __name__ == '__main__':
    main()
