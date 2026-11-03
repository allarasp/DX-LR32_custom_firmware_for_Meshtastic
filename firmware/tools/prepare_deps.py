"""Fetch pinned source repositories and extract device files; never accesses USB."""
import hashlib
import json
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]
manifest = json.loads((ROOT / "docs/source-lock.json").read_text())

for dependency in manifest["repositories"]:
    dest = ROOT / dependency["path"]
    url, commit = dependency["url"], dependency["commit"]
    if not dest.exists():
        dest.mkdir(parents=True)
        subprocess.run(["git", "init", str(dest)], check=True)
        subprocess.run(["git", "-C", str(dest), "remote", "add", "origin", url], check=True)
        subprocess.run(["git", "-C", str(dest), "config", "remote.origin.promisor", "true"], check=True)
        subprocess.run(["git", "-C", str(dest), "config", "remote.origin.partialclonefilter", "blob:none"], check=True)
        # Fetch only required source blobs on a fresh checkout. Preserve existing
        # full checkouts; never prune or reset a developer's dependency tree.
        subprocess.run(["git", "-C", str(dest), "sparse-checkout", "set", "--no-cone", *dependency["sparse_paths"]], check=True)
        subprocess.run(["git", "-C", str(dest), "fetch", "--filter=blob:none", "--depth=1", "origin", commit], check=True)
        subprocess.run(["git", "-C", str(dest), "checkout", "--detach", commit], check=True)
    actual = subprocess.check_output(["git", "-C", str(dest), "rev-parse", "HEAD"], text=True).strip()
    assert actual == commit, f"Unexpected revision in {dest}; refusing to change existing checkout"
    assert not subprocess.check_output(["git", "-C", str(dest), "status", "--porcelain"]), f"Modified dependency: {dest}"
    print(dependency["path"], actual)

pack = ROOT / "vendor/cx32l003-mirror/keil_CX32L003_pack/XMC.CX32L003_DFP.1.0.7.pack"
assert hashlib.sha256(pack.read_bytes()).hexdigest() == manifest["files"][str(pack.relative_to(ROOT))]
device_files = {
    "Device/Include/cx32l003.h",
    "Device/Include/system_cx32l003.h",
    "Device/Source/system_cx32l003.c",
    "XMC.CX32L003_DFP.pdsc",
}
with zipfile.ZipFile(pack) as archive:
    for name in archive.namelist():
        # Source only; never install or invoke the FLM flash algorithms.
        if name not in device_files:
            continue
        relative = Path(name)
        assert not relative.is_absolute() and ".." not in relative.parts
        dest = ROOT / "vendor/cx32l003-dfp" / relative
        content = archive.read(name)
        if dest.exists():
            assert dest.read_bytes() == content, f"Modified extracted file: {dest}"
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(content)
print("Pinned source dependencies ready; no target communication")
