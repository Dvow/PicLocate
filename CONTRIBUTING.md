# Contributing

Follow [Build](README.md#build) and [module boundaries](docs/architecture.md). Processing stays local; networking belongs in model downloads and updates.

## Checks

- Format C++ with clang-format **23.1.1**; verify using `--dry-run --Werror`.
- Run `ctest --test-dir <build-dir> --output-on-failure`. Defaults: `.build/cpm-release` on Windows, `.build/native-release` elsewhere.
- Verify deployment with `scripts/test-package.py`; packaging changes also need installer/native update checks. Use isolated test directories.
- Evaluate retrieval changes with representative images and human judgments. Optional fixture, throughput and model parity tools live in `tests/`; dependencies are in `tests/requirements-reference.txt`.

## Design

- Keep widgets on the GUI thread and SQLite connections on their owning threads. Bound worker queues and caches.
- Preserve favorites/descriptions during migrations; version changed embeddings.
- Retain CPU fallback and runtime AVX2 detection; avoid global architecture-specific flags.
- Pin dependencies and hashes. Exclude libraries, weights, images, databases and credentials from Git.

Actions retain test logs as artifacts. See [platform status](docs/compatibility.md).
