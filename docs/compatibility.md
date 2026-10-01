# Compatibility

Requires a 64-bit OS/CPU. GPU and AVX2 are optional. Windows provides DirectML indexing and OCR; macOS/Linux use CPU.

| Target | Baseline | Local validation |
|---|---|---|
| Windows x64 | Windows 10 22H2 / 11 | Windows 11 CPU/GPU |
| Windows ARM64 | Windows 11 | Pending |
| macOS Intel | 13.4+ | Pending |
| macOS Apple Silicon | 14+ | Pending |
| Linux x64 | Release CI: Ubuntu 22.04, X11/XWayland | Ubuntu 26.04 / Garuda WSL |
| Linux ARM64 | Ubuntu 24.04 / glibc 2.39+ | Pending |

The local Linux development archive requires **glibc 2.43**. Release CI and native desktop checks remain pending. Windows binaries are unsigned; macOS signing/notarization is pending.

Alpine/musl, 32-bit systems and Windows 7/8 are unsupported. Linux needs X11/XWayland. RAW, HEIC, AVIF and video are unsupported.

## Build prerequisites

CMake 3.24+, Ninja and C++20. CPM supplies pinned SDKs/models; Python is needed only for verification tools.

- **Windows:** Visual Studio C++ tools and Windows SDK. `build.ps1 -Architecture arm64` selects ARM64; cross-builds require `-SkipTests`, then native testing.
- **macOS:** Xcode command-line tools; one architecture per app/DMG.
- **Ubuntu:** install dependencies, then follow [Build](../README.md#build):

```sh
sudo apt-get install build-essential cmake ninja-build libgl-dev libegl-dev \
  libfontconfig1-dev libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 \
  libxcb-icccm4 libxcb-keysyms1 libxcb-shape0 libxcb-xinerama0 libxcb-xkb1 \
  libxcb-image0 libxcb-render-util0 libxcb-randr0 libxcb-sync1 libxcb-xfixes0 \
  libxcb-util1 libdbus-1-3 patchelf xvfb xauth
```

Linux packages include Qt/ICU/ONNX/models; the OS supplies desktop, C/C++ and TLS libraries. Run `PicLocate.sh` after extraction.

## Cache and overrides

Cache defaults: Windows `%LOCALAPPDATA%/PicLocateBuild/cache`, macOS `~/Library/Caches/PicLocateBuild`, Linux `${XDG_CACHE_HOME:-~/.cache}/PicLocateBuild`.

CMake accepts `PICLOCATE_DEPENDENCY_CACHE`, `CPM_SOURCE_CACHE`, `PICLOCATE_QT_ROOT` and `ONNXRUNTIME_ROOT`. Windows exposes `-DependencyCache`; `build.sh` honors `PICLOCATE_BUILD_DIR`, `PICLOCATE_PACKAGE_DIR` and `PICLOCATE_BUILD_JOBS`.

SDK pins: `cmake/Dependencies.cmake`, `scripts/qt-sdks.json`. Baselines follow [Qt support](https://doc.qt.io/qt-6.11/supported-platforms.html) and ONNX binaries: CPU 1.23.2 on Intel macOS, 1.30.0 elsewhere.
