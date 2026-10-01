# Privacy

Images, queries, OCR, embeddings and descriptions stay on your device. No accounts, analytics or synchronization. ONNX Runtime telemetry is disabled.

## Network

Settings downloads/verifies pinned models from Hugging Face over HTTPS with SHA-256 checks. Search/indexing works offline afterward; reference images are read locally.

Release builds check GitHub daily; disable checks in **Settings → Updates**. Downloads require a click and pass size/hash checks. GitHub receives your IP address/app version, never library data. Local builds require a configured release repository. Builds download dependencies through CPM.

## Storage

| OS | Default data directory |
|---|---|
| Windows | `%LOCALAPPDATA%/PicLocate/PicLocate` |
| Linux | `$XDG_DATA_HOME/PicLocate/PicLocate` or `~/.local/share/PicLocate/PicLocate` |
| macOS | `~/Library/Application Support/PicLocate/PicLocate` |

`--data-dir` selects another library. Storage contains SQLite/WAL/SHM files, settings, previews, models, updates and scan logs. Data is unencrypted; paths may appear in logs/screenshots. Keep it out of Git.

Indexing never moves/deletes originals. Disconnecting folders removes indexed records, without secure erasure. Uninstall preserves data. To reset, close PicLocate and delete its data directory; original folders remain untouched.
