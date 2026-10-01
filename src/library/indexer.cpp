#include "library/indexer.h"
#include "library/database.h"
#include "models/appearance.h"
#include "models/inference.h"
#include <QtConcurrent>
#include <deque>
namespace piclocate {
Indexer::Indexer(Paths paths) : paths_(std::move(paths)) {}
Indexer::~Indexer() = default;
void Indexer::scan(QStringList folders, bool ocr, bool force, bool appearanceOnly, bool useGpu) {
    int changed = 0, skipped = 0, failed = 0, done = 0;
    QStringList errors;
    QElapsedTimer scanTimer;
    scanTimer.start();
    metrics_ = {};
    qint64 decodeUs = 0, pixelsUs = 0, appearanceUs = 0, ocrUs = 0, inferenceUs = 0, writeUs = 0,
           labelsUs = 0;
    int inferenceImages = 0, cacheHits = 0, batches = 0;
    bool backendPrepared = false;
    if (force)
        embeddingCache_.clear();
    if (engine_ && !useGpu && engine_->usesGpu()) {
        engine_.reset();
        embeddingCache_.clear();
    }
    // Bounded prefetch overlaps decoding/OCR with one fixed-shape GPU session.
    // CPU fallback keeps the original single-image model computation.
    QThreadPool pool;
    pool.setMaxThreadCount(qBound(1, QThread::idealThreadCount() / 3, 4));
    struct Job {
        QFileInfo file;
        Photo photo;
        bool embed;
    };
    struct Prepared {
        Photo photo;
        std::vector<float> pixels, appearance;
        QByteArray thumbnail;
        QString error;
        QByteArray hash;
        qint64 decodeUs = 0, pixelsUs = 0, appearanceUs = 0, ocrUs = 0;
    };
    try {
        Database db(paths_);
        const auto existing = db.scanEntries();
        QList<Job> jobs;
        QHash<QString, QSet<QString>> seen;
        QSet<QString> reachable, unique, supported;
        for (const auto &format : QImageReader::supportedImageFormats())
            supported.insert(QString::fromLatin1(format).toLower());
        emit stage(appearanceOnly ? QStringLiteral("Preparing the appearance index…")
                                  : QStringLiteral("Discovering images…"));
        for (const auto &folder : folders) {
            if (cancelled_)
                break;
            if (!QFileInfo(folder).isDir() || !QFileInfo(folder).isReadable()) {
                errors << QStringLiteral("Folder unavailable; previous index retained: ") + folder;
                continue;
            }
            reachable.insert(folder);
            QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot,
                            QDirIterator::Subdirectories);
            while (it.hasNext() && !cancelled_) {
                it.next();
                const auto file = it.fileInfo();
                if (!supported.contains(file.suffix().toLower()))
                    continue;
                // Roots are canonical and directory symlinks are not followed.
                // Avoid opening every ordinary file to canonicalize its path again.
                const auto path = file.isSymLink() ? normalizedPath(file.absoluteFilePath())
                                                   : QDir::cleanPath(file.absoluteFilePath());
                seen[folder].insert(path);
                if (unique.contains(path))
                    continue;
                unique.insert(path);
                const auto fingerprint =
                    qMakePair(file.lastModified().toMSecsSinceEpoch(), file.size());
                const auto entry = existing.value(path);
                Photo photo = entry.photo;
                const bool sameFile = qMakePair(photo.modified, photo.bytes) == fingerprint;
                if (appearanceOnly && !photo.id)
                    continue;
                if (appearanceOnly && qMakePair(photo.modified, photo.bytes) != fingerprint) {
                    ++skipped;
                    continue; // A normal Rescan handles new/changed originals.
                }
                const bool embed =
                    !appearanceOnly && (force || !sameFile || !entry.embeddingCurrent);
                if (!embed && sameFile && entry.appearanceCurrent) {
                    ++skipped;
                    continue;
                }
                photo.path = path;
                photo.folder = folder;
                photo.name = file.fileName();
                photo.modified = fingerprint.first;
                photo.bytes = fingerprint.second;
                jobs.append({file, photo, embed});
            }
        }
        const int total = skipped + int(jobs.size());
        done = skipped;
        emit progress(done, total, {}, changed, skipped);
        std::deque<QFuture<Prepared>> pending;
        qsizetype next = 0;
        const auto prepare = [ocr](Job job) {
            Prepared out;
            out.photo = std::move(job.photo);
            try {
                QElapsedTimer timer;
                timer.start();
                QImageReader reader(out.photo.path);
                reader.setAutoTransform(true);
                // A descriptor needs only a small preview. Full image tensors
                // retain the original preprocessing for embedding compatibility.
                if (!job.embed && qMax(reader.size().width(), reader.size().height()) > 512)
                    reader.setScaledSize(reader.size().scaled(512, 512, Qt::KeepAspectRatio));
                const auto image = reader.read();
                if (image.isNull())
                    throw std::runtime_error(reader.errorString().toStdString());
                out.decodeUs = timer.nsecsElapsed() / 1000;
                timer.restart();
                out.appearance = appearanceDescriptor(
                    qMax(image.width(), image.height()) > 512
                        ? image.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                        : image);
                out.appearanceUs = timer.nsecsElapsed() / 1000;
                timer.restart();
                if (job.embed) {
                    out.pixels = Inference::preprocess(image);
                    // This ephemeral cache fingerprints exact tensor bytes.
                    // BLAKE2b keeps a 256-bit cryptographic key without the
                    // per-image cost of software SHA-256 on the large tensor.
                    out.hash = QCryptographicHash::hash(
                        QByteArrayView(reinterpret_cast<const char *>(out.pixels.data()),
                                       qsizetype(out.pixels.size() * sizeof(float))),
                        QCryptographicHash::Blake2b_256);
                    out.pixelsUs = timer.nsecsElapsed() / 1000;
                    timer.restart();
                    out.photo.width = image.width();
                    out.photo.height = image.height();
                    if (ocr)
                        out.photo.ocr = recognizeText(image);
                    out.ocrUs = timer.nsecsElapsed() / 1000;
                    const auto thumbnail =
                        image.width() > 420 || image.height() > 320
                            ? image.scaled(420, 320, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                            : image;
                    QBuffer encoded(&out.thumbnail);
                    encoded.open(QIODevice::WriteOnly);
                    QImageWriter writer(&encoded, image.hasAlphaChannel() ? QByteArray("png")
                                                                          : QByteArray("jpg"));
                    if (image.hasAlphaChannel())
                        writer.setCompression(1);
                    else
                        writer.setQuality(84);
                    if (!writer.write(thumbnail))
                        throw std::runtime_error("Thumbnail could not be encoded");
                }
                // Do not commit a descriptor for a file modified during decoding.
                const QFileInfo after(out.photo.path);
                if (after.size() != out.photo.bytes ||
                    after.lastModified().toMSecsSinceEpoch() != out.photo.modified)
                    throw std::runtime_error("File changed while indexing; rescan to retry");
            } catch (const std::exception &e) {
                out.error = QString::fromUtf8(e.what());
            }
            return out;
        };
        const auto enqueue = [&] {
            while (!cancelled_ && next < jobs.size() && pending.size() < 32)
                pending.push_back(QtConcurrent::run(&pool, prepare, jobs[next++]));
        };
        enqueue();
        QElapsedTimer ui;
        ui.start();
        QElapsedTimer refresh;
        refresh.start();
        while (!pending.empty() && !cancelled_) {
            std::vector<Prepared> batch;
            while (!pending.empty() && batch.size() < GpuVision::BatchSize && !cancelled_) {
                batch.push_back(pending.front().result());
                pending.pop_front();
                enqueue();
            }
            if (cancelled_)
                break;
            std::vector<std::vector<float>> tensors;
            std::vector<int> positions(batch.size(), -1);
            std::vector<CachedEmbedding> embeddings(batch.size());
            QHash<QByteArray, int> uniqueInputs;
            for (size_t i = 0; i < batch.size(); ++i) {
                auto &item = batch[i];
                decodeUs += item.decodeUs;
                pixelsUs += item.pixelsUs;
                appearanceUs += item.appearanceUs;
                ocrUs += item.ocrUs;
                if (!item.error.isEmpty() || item.pixels.empty())
                    continue;
                if (auto *cached = embeddingCache_.object(item.hash)) {
                    embeddings[i] = *cached;
                    ++cacheHits;
                } else if (uniqueInputs.contains(item.hash)) {
                    positions[i] = uniqueInputs.value(item.hash);
                    ++cacheHits;
                } else {
                    positions[i] = int(tensors.size());
                    uniqueInputs.insert(item.hash, positions[i]);
                    tensors.push_back(std::move(item.pixels));
                }
            }
            if (!tensors.empty()) {
                if (!backendPrepared) {
                    emit stage(QStringLiteral("Preparing the local inference engine…"));
                    if (!engine_)
                        engine_ = std::make_unique<Inference>(paths_.models());
                    if (useGpu)
                        engine_->enableGpu();
                    backendPrepared = true;
                    emit stage(QStringLiteral("Indexing · ") + engine_->backend());
                    emit backendChanged(engine_->backend());
                }
                QElapsedTimer timer;
                timer.start();
                auto vectors = engine_->imageBatch(tensors);
                emit backendChanged(engine_->backend());
                inferenceUs += timer.nsecsElapsed() / 1000;
                inferenceImages += int(tensors.size());
                ++batches;
                for (size_t i = 0; i < batch.size(); ++i)
                    if (positions[i] >= 0) {
                        embeddings[i] = {vectors[size_t(positions[i])], engine_->embeddingModel()};
                        embeddingCache_.insert(batch[i].hash, new CachedEmbedding(embeddings[i]));
                    }
            }
            // Inference and the cold label model can take seconds. Do all of
            // that before acquiring SQLite's writer lock, so favorites and
            // descriptions remain writable while indexing continues.
            for (size_t i = 0; i < batch.size() && !cancelled_; ++i) {
                auto &item = batch[i];
                if (!item.error.isEmpty() || embeddings[i].vector.empty())
                    continue;
                QElapsedTimer timer;
                timer.start();
                try {
                    item.photo.tags = engine_->labels(embeddings[i].vector);
                } catch (const std::exception &e) {
                    item.error = QString::fromUtf8(e.what());
                }
                labelsUs += timer.nsecsElapsed() / 1000;
            }
            db.begin();
            for (size_t i = 0; i < batch.size() && !cancelled_; ++i) {
                auto &item = batch[i];
                ++done;
                try {
                    if (!item.error.isEmpty())
                        throw std::runtime_error(item.error.toStdString());
                    if (!embeddings[i].vector.empty()) {
                        QElapsedTimer timer;
                        timer.start();
                        item.photo.thumb = db.saveThumbnail(item.thumbnail);
                        db.save(item.photo, embeddings[i].vector, embeddings[i].model);
                        writeUs += timer.nsecsElapsed() / 1000;
                    }
                    db.saveAppearance(item.photo, item.appearance);
                    ++changed;
                } catch (const std::exception &e) {
                    ++failed;
                    if (errors.size() < 30)
                        errors << item.photo.name + QStringLiteral(": ") +
                                      QString::fromUtf8(e.what());
                }
                if (ui.elapsed() > 100 || done == total) {
                    emit progress(done, total, item.photo.name, changed, skipped);
                    ui.restart();
                }
            }
            db.commit();
            if (changed && changed % 512 == 0 && refresh.elapsed() >= 2000) {
                emit batchCommitted();
                refresh.restart();
            }
        }
        db.begin();
        if (!cancelled_ && !appearanceOnly) {
            for (const auto &folder : reachable)
                db.prune(folder, seen[folder]);
            db.pruneThumbnails();
        }
        db.commit();
    } catch (const std::exception &e) {
        ++failed;
        errors << QString::fromUtf8(e.what());
    }
    pool.waitForDone();
    metrics_ = {{QStringLiteral("wall_ms"), scanTimer.elapsed()},
                {QStringLiteral("backend"), engine_ && backendPrepared
                                                ? engine_->backend()
                                                : QStringLiteral("No inference needed")},
                {QStringLiteral("gpu_fallback"),
                 engine_ && backendPrepared ? engine_->gpuFailure() : QString()},
                {QStringLiteral("inference_images"), inferenceImages},
                {QStringLiteral("reused_embeddings"), cacheHits},
                {QStringLiteral("batches"), batches},
                {QStringLiteral("decode_ms"), decodeUs / 1000.},
                {QStringLiteral("preprocess_ms"), pixelsUs / 1000.},
                {QStringLiteral("appearance_ms"), appearanceUs / 1000.},
                {QStringLiteral("ocr_ms"), ocrUs / 1000.},
                {QStringLiteral("inference_ms"), inferenceUs / 1000.},
                {QStringLiteral("labels_ms"), labelsUs / 1000.},
                {QStringLiteral("write_ms"), writeUs / 1000.}};
    // Indexing should not reserve GPU/vision-model memory while the app is idle.
    if (engine_)
        engine_->releaseVision();
    emit completed(changed, skipped, failed, cancelled_, errors);
}
} // namespace piclocate
