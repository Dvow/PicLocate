#!/usr/bin/env python3
"""Test package-manager installation in a disposable Linux system/container."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def run(*args):
    subprocess.run(args, check=True, timeout=600)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('package', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    package = args.package.resolve(strict=True)
    arch = package.name.endswith('.pkg.tar.zst')
    manager = 'pacman' if arch else 'apt'
    query = ['pacman', '-Q', 'piclocate'] if arch else ['dpkg-query', '-W', '-f=${db:Status-Status}', 'piclocate']
    existing = subprocess.run(query, capture_output=True, text=True)
    if (arch and existing.returncode == 0) or (not arch and existing.stdout == 'installed'):
        parser.error('PicLocate is already installed; use a disposable system')
    if os.geteuid() != 0 or args.output.exists():
        parser.error('Use root in a disposable system and a fresh output directory')
    output = args.output.resolve()
    output.mkdir(parents=True)
    user = 'nobody'
    with tempfile.TemporaryDirectory(prefix='piclocate-install-test-') as temporary:
        work = Path(temporary)
        home = work / 'home'
        home.mkdir()
        run('chown', user, str(work), str(home))
        work.chmod(0o755)
        install = ['pacman', '-U', '--noconfirm', str(package)] if arch else ['apt-get', 'install', '-y', str(package)]
        run(*install)
        app = Path('/opt/piclocate')
        launcher = Path('/usr/bin/piclocate')
        assert launcher.resolve() == app / 'bin/PicLocate'
        assert 'Exec=piclocate' in Path('/usr/share/applications/piclocate.desktop').read_text()
        assert Path('/usr/share/icons/hicolor/scalable/apps/piclocate.svg').is_file()
        assert Path('/usr/share/licenses/piclocate/LICENSE').is_file()
        for path in [app / 'bin/PicLocate', Path('/usr/share/applications/piclocate.desktop'),
                     Path('/usr/share/icons/hicolor/scalable/apps/piclocate.svg')]:
            assert not path.stat().st_mode & 0o022, f'Package files must not be writable by other users: {path}'
        assert (app / 'share/PicLocate/.piclocate-system-package').read_text().strip() == ('arch' if arch else 'deb')
        test = Path(__file__).with_name('test-package.py').resolve()
        checks = work / 'checks'
        try:
            run('runuser', '-u', user, '--', 'env', f'HOME={home}', 'QT_QPA_PLATFORM=xcb', 'xvfb-run', '-a', sys.executable,
                str(test), str(launcher), '--output', str(checks), '--screenshot')
        finally:
            if checks.exists():
                shutil.copytree(checks, output / 'checks')
        result = json.loads((checks / 'result.json').read_text())
        assert result['diagnostics']['update_platform'].endswith('-arch' if arch else '-deb')
        data = checks / 'library'
        settings = data / 'settings.ini'
        settings.write_text('[PackageVerification]\npreserve=true\n')
        before = [digest(data / 'library.sqlite'), digest(settings)]
        user_file = app / 'user-created-file.txt'
        user_file.write_text('Preserve files not owned by the package.\n')
        run(*(['pacman', '-U', '--noconfirm', str(package)] if arch else ['apt-get', 'install', '--reinstall', '-y', str(package)]))
        assert before == [digest(data / 'library.sqlite'), digest(settings)]
        run(*(['pacman', '-R', '--noconfirm', 'piclocate'] if arch else ['apt-get', 'remove', '-y', 'piclocate']))
        assert not launcher.exists() and not (app / 'bin/PicLocate').exists()
        assert not Path('/usr/share/applications/piclocate.desktop').exists()
        assert user_file.is_file()
        assert before == [digest(data / 'library.sqlite'), digest(settings)]
        (output / 'result.json').write_text(json.dumps(dict(manager=manager, installed=True, searched=True,
            reinstalled=True, uninstalled=True, library_preserved=True, settings_preserved=True,
            user_file_preserved=True), indent=2) + '\n')
    print(f'{manager} package lifecycle passed: {output}')


if __name__ == '__main__':
    main()
