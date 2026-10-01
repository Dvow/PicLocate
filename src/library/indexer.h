#pragma once
#include "core/types.h"
#include <QCache>
#include <QJsonObject>
#include <QObject>
#include <atomic>
#include <memory>
#include <vector>

namespace piclocate {
class Inference;
class Indexer : public QObject {
    Q_OBJECT
  public:
    explicit Indexer(Paths paths);
    ~Indexer() override;
    void cancel() { cancelled_ = true; }
    void prepare() { cancelled_ = false; }
    QJsonObject metrics() const { return metrics_; }
  public slots:
    void scan(QStringList folders, bool ocr, bool force, bool appearanceOnly = false,
              bool useGpu = true);
  signals:
    void progress(int done, int total, QString name, int changed, int skipped);
    void stage(QString text);
    void backendChanged(QString name);
    void batchCommitted();
    void completed(int changed, int skipped, int failed, bool cancelled, QStringList errors);

  private:
    Paths paths_;
    std::atomic_bool cancelled_{false};
    std::unique_ptr<Inference> engine_;
    QJsonObject metrics_;
    struct CachedEmbedding {
        std::vector<float> vector;
        QString model;
    };
    QCache<QByteArray, CachedEmbedding> embeddingCache_{8192};
};
} // namespace piclocate
