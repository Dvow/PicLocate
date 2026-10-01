# Third-party notices

PicLocate code is [MIT licensed](LICENSE). Dependencies retain their own licenses. Preserve packaged notices and sources when redistributing binaries.

| Component | Version | License |
|---|---|---|
| CPM.cmake (build only) | 0.43.2 | MIT |
| Inno Setup | 7.1.0 | Inno Setup License |
| Qt Core/Gui/Widgets/Network/Sql and image/SVG plugins | 6.11.2 | LGPL-3.0 / GPL alternatives; bundled dependency notices |
| ONNX Runtime CPU | 1.30.0; Intel macOS 1.23.2 | MIT and third-party notices |
| ONNX Runtime DirectML | 1.24.4 | MIT and third-party notices |
| Microsoft DirectML | 1.15.4 | Microsoft redistributable terms |
| OpenAI CLIP and pinned ONNX/tokenizer export | ViT-B/32 | Upstream CLIP MIT license |
| SQLite through Qt SQL | Qt-bundled revision | Public domain |
| ICU on Linux | Matching Qt SDK | Unicode license |
| Microsoft Visual C++ runtime | Installed MSVC redist | Microsoft redistributable terms |

## Qt

Qt is dynamically linked. You may replace its libraries/plugins with compatible modified versions and rebuild PicLocate. Reverse engineering to debug modifications to LGPL components is permitted.

The resource directory contains unmodified matching **qtbase**, **qtsvg** and **qtimageformats** archives in `third-party-source/`, including build instructions and licensing metadata. Complete license directories accompany them under `licenses/`. Resource locations: Windows package root, Linux `share/PicLocate`, macOS `Contents/Resources`. Source checksums are in `scripts/qt-sources.json`.

Keep these archives/notices and PicLocate source available with releases. Do not prohibit Qt modification, replacement or the associated debugging rights. See [Qt sources](https://download.qt.io/archive/qt/6.11/6.11.2/submodules/), [build instructions](https://doc.qt.io/qt-6/configure-options.html) and [LGPL obligations](https://www.qt.io/licensing/open-source-lgpl-obligations).

## Runtimes and models

Packages include original ONNX Runtime, DirectML and [CLIP license](https://github.com/openai/CLIP/blob/main/LICENSE) files under `licenses/`. The GPU runtime is renamed `onnxruntime-dml.dll`; its notices remain unchanged.

Model source: [Xenova/clip-vit-base-patch32](https://huggingface.co/Xenova/clip-vit-base-patch32/tree/d15189d7028b43f1d3e65039190477f6af591c2a), revision `d15189d7028b43f1d3e65039190477f6af591c2a`. Models are hash-verified and excluded from source control. Consult the upstream model card for limitations.

App-local Microsoft CRT files retain Microsoft's redistribution terms; see the [Visual C++ Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

Inno Setup retains its engine and notices: © 1997–2026 Jordan Russell; portions © 2000–2026 Martijn Laan. See its [license](https://github.com/jrsoftware/issrc/blob/is-7_1_0/license.txt).

## Screenshot

The documentation preview uses generated fixtures and public-domain/CC0 [scikit-image samples](https://github.com/scikit-image/scikit-image/blob/v0.25.2/skimage/data/_fetchers.py): NASA astronaut/Hubble, SpaceX rocket, coffee and Chelsea the cat. Fixture originals are not shipped.
