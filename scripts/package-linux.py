#!/usr/bin/env python3
"""Create apt/pacman packages from the deployed Linux bundle."""
import argparse
import re
import shutil
import subprocess
import tarfile
import tempfile
from pathlib import Path

from release import ROOT, checksum, project_version


def run(*args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def elf_files(root):
    result = []
    for path in root.rglob('*'):
        if path.is_file() and not path.is_symlink():
            with path.open('rb') as stream:
                if stream.read(4) == b'\x7fELF':
                    result.append(path)
    return result


def package(bundle, output):
    version = project_version()
    target = (bundle / 'share/PicLocate/.piclocate-package').read_text().strip()
    if target not in ('linux-x64', 'linux-arm64'):
        raise ValueError('Expected a deployed Linux bundle')
    debian = Path('/var/lib/dpkg/status').is_file() and shutil.which('dpkg-shlibdeps')
    arch = target == 'linux-x64' and shutil.which('makepkg')
    if not debian and not arch:
        print('Portable package ready; install dpkg-dev/makepkg for system packages.')
        return
    for path in bundle.rglob('*'):
        if path.name in ('settings.ini', 'thumbnails') or '.sqlite' in path.name:
            raise ValueError(f'User data must never enter a package: {path}')
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='piclocate-packaging-') as temporary:
        work = Path(temporary)
        root = work / 'root'
        app = root / 'opt/piclocate'
        shutil.copytree(bundle, app, symlinks=True)
        marker = app / 'share/PicLocate/.piclocate-system-package'
        marker.touch()
        launcher = root / 'usr/bin/piclocate'
        launcher.parent.mkdir(parents=True)
        launcher.symlink_to('../../opt/piclocate/bin/PicLocate')
        for relative in ('applications/piclocate.desktop', 'icons/hicolor/scalable/apps/piclocate.svg'):
            destination = root / 'usr/share' / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(bundle / 'share' / relative, destination)
        license_path = root / 'usr/share/licenses/piclocate/LICENSE'
        license_path.parent.mkdir(parents=True)
        shutil.copyfile(bundle / 'share/PicLocate/LICENSE', license_path)
        # Windows-mounted build trees may expose every file as writable/executable.
        root.chmod(0o755)
        for path in root.rglob('*'):
            if not path.is_symlink():
                path.chmod(0o755 if path.is_dir() else 0o644)
        (app / 'bin/PicLocate').chmod(0o755)
        for path in app.rglob('*.sh'):
            path.chmod(0o755)
        files = elf_files(app)
        if debian:
            marker.write_text('deb\n')
            metadata = root / 'DEBIAN'
            metadata.mkdir()
            # Resolve host runtime requirements while ignoring bundled Qt/ONNX libraries.
            control = work / 'debian/control'
            control.parent.mkdir()
            control.write_text('Source: piclocate\n\nPackage: piclocate\nArchitecture: any\nDescription: Private image search\n')
            dependencies = run('dpkg-shlibdeps', '-O', '--ignore-missing-info', f'-l{app / "lib"}',
                               *[f'-e{path}' for path in files], cwd=work, stdout=subprocess.PIPE).stdout.strip()
            if not dependencies.startswith('shlibs:Depends='):
                raise ValueError('Could not determine Debian runtime dependencies')
            dependencies = dependencies.removeprefix('shlibs:Depends=') + ', ca-certificates, libssl3 | libssl3t64'
            architecture = 'amd64' if target == 'linux-x64' else 'arm64'
            size = sum(p.stat().st_size for p in root.rglob('*') if p.is_file() and not p.is_symlink()) // 1024
            (metadata / 'control').write_text(
                f'Package: piclocate\nVersion: {version}-1\nArchitecture: {architecture}\n'
                f'Maintainer: Dvow <177583401+Dvow@users.noreply.github.com>\nSection: graphics\nPriority: optional\n'
                f'Homepage: https://github.com/Dvow/PicLocate\nInstalled-Size: {size}\nDepends: {dependencies}\n'
                'Description: Private image search\n Search images by description, filename or visual similarity.\n')
            run('dpkg-deb', '--root-owner-group', '-Zgzip', '--build', str(root), str(output / f'PicLocate-{version}-{target}.deb'))
            shutil.rmtree(metadata)
        if arch:
            marker.write_text('arch\n')
            payload = work / 'payload.tar'
            with tarfile.open(payload, 'w') as archive:
                archive.add(root, arcname='root')
            glibc = (0, 0)
            for path in files:
                symbols = run('readelf', '--version-info', str(path), capture_output=True).stdout
                versions = [tuple(map(int, v.split('.'))) for v in re.findall(r'\bGLIBC_([0-9.]+)\b', symbols)]
                glibc = max([glibc, *versions])
            dependencies = [f'glibc>={".".join(map(str, glibc))}', 'gcc-libs', 'brotli', 'krb5', 'zstd', 'zlib',
                            'libglvnd', 'libxkbcommon-x11', 'fontconfig', 'glib2', 'freetype2', 'dbus', 'libxcb',
                            'libx11', 'xcb-util-cursor', 'xcb-util-image', 'xcb-util-keysyms', 'xcb-util-renderutil',
                            'xcb-util-wm', 'openssl', 'ca-certificates']
            recipe = (ROOT / 'packaging/linux/PKGBUILD.in').read_text()
            for key, value in {'VERSION': version, 'DEPENDS': ' '.join(f"'{d}'" for d in dependencies),
                               'SHA256': checksum(payload)}.items():
                recipe = recipe.replace(f'@{key}@', value)
            (work / 'PKGBUILD').write_text(recipe)
            config = work / 'makepkg.conf'
            config.write_text(Path('/etc/makepkg.conf').read_text() +
                              '\nPKGEXT=".pkg.tar.zst"\nCOMPRESSZST=(zstd -c -T0 -3 -)\n')
            run('makepkg', '--config', str(config), '--nodeps', '--noconfirm', cwd=work)
            packages = list(work.glob('piclocate-*.pkg.tar.zst'))
            if len(packages) != 1:
                raise ValueError('Expected exactly one Arch package')
            shutil.copy2(packages[0], output / f'PicLocate-{version}-{target}.pkg.tar.zst')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('bundle', type=Path)
    parser.add_argument('--output', type=Path, default=ROOT / 'dist/packages')
    args = parser.parse_args()
    package(args.bundle.resolve(strict=True), args.output.resolve())


if __name__ == '__main__':
    main()
