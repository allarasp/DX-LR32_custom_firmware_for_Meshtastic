"""Check the recorded dependency bytes without networking or target access."""
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
manifest = json.loads((root / "docs/source-lock.json").read_text())
for name, expected in manifest["files"].items():
    actual = hashlib.sha256((root / name).read_bytes()).hexdigest()
    assert actual == expected, f"Source hash mismatch: {name}"
print(f"Source hashes verified: {len(manifest['files'])} files")
