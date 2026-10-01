# Architecture

| Module | Responsibility |
|---|---|
| `src/app` | Startup, CLI and headless commands |
| `src/core` | Types, paths, image/OCR utilities, hashing and vector kernels |
| `src/library` | SQLite storage, indexing and ranking |
| `src/models` | CLIP/tokenizer, CPU/DirectML inference, appearance descriptors and downloads |
| `src/ui` | Window, settings, gallery, viewer and theme |
| `src/updates` | Release metadata, HTTPS downloads and verified installation |

`piclocate_core` serves desktop/CLI; `piclocate_ui` adds widgets; `piclocate_updates` uses Qt Core/Network. UI interfaces hide SQL/ONNX details.

## Indexing

Workers own SQLite connections and lazy model sessions. Up to four preparation workers feed bounded inference batches of 16. Windows DirectML retries smaller batches/adapters before CPU fallback. Other platforms use CPU.

Fingerprints skip unchanged files. Normalized-input hashes reuse embeddings without merging records. Batches commit outside inference; cancelled scans retain completed work. Unavailable roots retain their index. Vision sessions close after scanning.

SQLite uses WAL, versioned vectors, deduplicated previews and trigger-maintained FTS5. Migrations preserve notes/favorites and reject future schemas. A process lock prevents simultaneous library writers.

## Search

CLIP supplies normalized 512D vectors. Exact cosine scoring selects AVX2 at runtime or portable C++. Smart search adds bounded filename/description/OCR boosts; text-only search skips inference. Scores are ranks, not probabilities. CLIP text is limited to 77 tokens.

Appearance descriptors combine color/alpha, gradients and luminance; shape/palette modes cache 256D projections. Transparent padding is ignored. These match nearby variants without rotation/background invariance.

Search sorts the first page, then continues cached results in 300-row pages without rescoring. Filters precede selection; generation/cursor checks reject stale results. Browsing avoids loading vectors.

Gallery painting creates no per-image widgets. Two thumbnail workers, a 32-request queue and ~64 MiB cache bound loading. Decode/viewer work is asynchronous. Description saves debounce and flush on selection changes/close.

## CLI

Use `--help` for options. Headless commands require `--headless`; isolate tests with `--data-dir`.

| Option | Purpose |
|---|---|
| `--index <folder>` | Index images |
| `--query <text>` / `--text-only` | Semantic/text search |
| `--similar <id>` / `--reference <file>` | Image search |
| `--appearance` / `--shape-match` / `--color-match` | Appearance intent |
| `--appearance-index` | Upgrade descriptors without inference |
| `--cpu` / `--no-ocr` | Disable GPU/OCR |
| `--report <file>` / `--benchmark` / `--diagnostics` | JSON results, timing or backend details |

`PICLOCATE_MODEL_DIR` overrides models; `PICLOCATE_DISABLE_AVX2=1` forces portable scoring. Models are hash-checked. Opaque images use center cropping; transparent assets preserve visible bounds. OCR uses installed Windows languages.
