"""Publish complete asset metadata despite transient Windows sync-file locks."""
import json
import os
from pathlib import Path
import tempfile
import time


def write_json(path, document):
    fd, name = tempfile.mkstemp(prefix='RiftAsset-', suffix='.json', dir=Path(path).parent)
    os.close(fd)
    staged = Path(name)
    try:
        staged.write_text(json.dumps(document, indent=2, allow_nan=False), encoding='utf-8')
        for attempt in range(12):
            try:
                os.replace(staged, path)
                return
            except OSError:
                if attempt == 11:
                    raise
                time.sleep(.25)
    finally:
        staged.unlink(missing_ok=True)
