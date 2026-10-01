# Packaging

`scripts/build.ps1` builds/tests and creates the portable folder, installer and checksum in `dist/`. Models, runtimes, licenses and Qt sources are bundled. CPM acquires pinned Inno Setup; no global install is needed.

| Windows option | Effect |
|---|---|
| `-Offline` | Reuse dependencies |
| `-SkipInstaller` | Portable only |
| `-WithoutModels` | Download models later |
| `-InstallerDirectory <path>` | Change setup output |
| `-Iscc <path>` | Existing Inno Setup 7.1+ compiler |

The CMake `setup` target packages without testing; `PICLOCATE_BUILD_INSTALLER=OFF` disables it. Setup installs per-user in `%LOCALAPPDATA%/Programs/PicLocate`, uses `AppId=PicLocate.LocalImageSearch` and preserves user data/files on upgrade/uninstall.

## Verify

```powershell
./scripts/test-installer.ps1 `
  -Installer ./dist/installers/PicLocate-1.7.2-Setup-x64.exe `
  -PayloadDirectory ./.build/cpm-release/installer-stage
```

Use an account without an existing installation. Tests check CPU search/fallback, reinstall, uninstall and preservation. `-PreviousInstaller <path>` adds upgrade coverage. Native helper: `python3 scripts/test-update.py`.

Linux builds add `.deb` packages (x64/ARM64) and `.pkg.tar.zst` (Arch x64). `scripts/package-linux.py` packages the deployed bundle using dpkg/makepkg. CI tests Debian and Arch installs in disposable containers.

## Releases and updates

Push to a public GitHub repository with `main` and Actions enabled. CI embeds the repository; `GITHUB_TOKEN` handles publishing without custom secrets.

Pushes/PRs check six targets. Bump `project(PicLocate VERSION ...)` on `main` to release. A matching `vX.Y.Z` tag at main's current commit or manual **Build and release → Publish** also triggers publication. Published versions are immutable; downgrades are rejected.

All checks must pass. Uploads remain draft until eleven packages, `SHA256SUMS` and `update.json` pass size/hash verification. Failed Actions drafts can resume at the next verified commit.

Settings offers **Download update → Install and restart**, with optional daily checks. Repository, architecture, version, size and SHA-256 are verified over HTTPS. Installers wait for exit. Windows uses Inno; writable Linux/macOS packages use staged replacement with a recovery copy. System-owned installs/libraries inside the app require manual updates.

apt/pacman installations download the matching package; **Show package** opens its folder for installation with the package manager. The app never replaces package-manager-owned files.

Local updates require `-UpdateRepository owner/repository` on Windows or `-DPICLOCATE_UPDATE_REPOSITORY=owner/repository` through `build.sh`; empty disables checks.
