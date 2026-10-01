#!/usr/bin/env python3
"""Exercise the real native helper with disposable applications and libraries."""
import os
import plistlib
import subprocess
import sys
import tarfile
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MAC = sys.platform == "darwin"
PLATFORM = ("macos" if MAC else "linux") + ("-arm64" if os.uname().machine in ("arm64", "aarch64") else "-x64")


def app(directory, version):
    resource = directory / ("Contents/Resources" if MAC else "share/PicLocate")
    resource.mkdir(parents=True)
    (resource / ".piclocate-package").write_text(PLATFORM + "\n")
    binary = directory / ("Contents/MacOS/PicLocate" if MAC else "bin/PicLocate")
    binary.parent.mkdir(parents=True)
    if MAC:
        source = directory.parent / f"fixture-{version}.c"
        source.write_text('#include <stdio.h>\n#include <string.h>\nint main(int argc,char**argv){ char p[4096]; '
                          f'if(argc!=3)return 1; snprintf(p,sizeof(p),"%s/restarted.txt",argv[2]); '
                          f'FILE*f=fopen(p,"w");if(!f)return 2;fputs("{version}",f);fclose(f);return 0;}}')
        subprocess.run(["cc", str(source), "-o", str(binary)], check=True)
        with (directory / "Contents/Info.plist").open("wb") as stream:
            plistlib.dump({"CFBundleName": "PicLocate", "CFBundleExecutable": "PicLocate", "CFBundlePackageType": "APPL",
                           "CFBundleIdentifier": "io.piclocate.update-test", "CFBundleVersion": version}, stream)
    else:
        binary.write_text(f'#!/bin/sh\nprintf %s {version} > "$2/restarted.txt"\n')
        binary.chmod(0o755)
    return binary


def wait_for(predicate, seconds=30):
    deadline = time.monotonic() + seconds
    while not predicate():
        if time.monotonic() >= deadline:
            raise AssertionError("Update/restart timed out")
        time.sleep(0.1)


def main():
    with tempfile.TemporaryDirectory(prefix="PicLocate update test ") as temporary:
        root = Path(temporary)
        target = root / ("PicLocate Installed.app" if MAC else "PicLocate Installed")
        old_binary = app(target, "1.7.1")
        (target / "keep me.txt").write_text("user file")
        library = root / "Library with spaces"
        library.mkdir()
        (library / "library.sqlite").write_bytes(b"library must remain unchanged")
        payload = root / "payload"
        candidate = payload / ("PicLocate.app" if MAC else f"PicLocate-1.8.0-{PLATFORM}")
        app(candidate, "1.8.0")
        archive = root / ("update.dmg" if MAC else "update.tar.gz")
        if MAC:
            subprocess.run(["hdiutil", "create", "-srcfolder", str(payload), "-format", "UDZO", "-ov", str(archive)], check=True, capture_output=True)
        else:
            with tarfile.open(archive, "w:gz") as stream:
                stream.add(candidate, arcname=candidate.name)
        old_bytes = old_binary.read_bytes()
        parent = subprocess.Popen([sys.executable, "-c", "import time;time.sleep(60)"])
        helper = None
        try:
            helper = subprocess.Popen(["/bin/sh", str(ROOT / "installer/update-unix.sh"), PLATFORM, str(archive),
                                       str(target), str(parent.pid), str(library)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            wait_for(lambda: any(root.glob(".piclocate-update.*/merged")))
            assert old_binary.read_bytes() == old_bytes, "Files replaced while app still running"
            assert not (library / "restarted.txt").exists()
            parent.terminate()
            parent.wait(timeout=10)
            output, _ = helper.communicate(timeout=30)
            assert helper.returncode == 0, output
            wait_for(lambda: (library / "restarted.txt").exists())
            assert (library / "restarted.txt").read_text() == "1.8.0"
            assert (library / "library.sqlite").read_bytes() == b"library must remain unchanged"
            assert (target / "keep me.txt").read_text() == "user file"
            assert any(root.glob(".piclocate-update.*/previous")), "Recovery copy missing"
            print(f"PASS {PLATFORM}: process wait, replacement, restart, recovery and data preservation")
        finally:
            if parent.poll() is None:
                parent.terminate()
                parent.wait(timeout=10)
            if helper is not None and helper.poll() is None:
                helper.terminate()
                helper.communicate(timeout=10)


if __name__ == "__main__":
    main()
