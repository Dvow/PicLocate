#pragma once
#include "core/types.h"
#include <QImage>
#include <QtSql>
#include <cstdint>
#include <memory>
#include <vector>

namespace piclocate {
class Database {
  public:
    explicit Database(const Paths &paths);
    ~Database();
    Database(const Database &) = delete;
    void initialize();
    QStringList folders() const;
    void addFolder(const QString &path);
    void removeFolder(const QString &path);
    int count() const;
    struct Stats {
        int images = 0, outdated = 0, appearances = 0;
        qint64 bytes = 0;
    };
    Stats stats() const;
    struct ScanEntry {
        Photo photo;
        bool embeddingCurrent = false, appearanceCurrent = false;
    };
    QHash<QString, ScanEntry> scanEntries() const;
    QHash<qint64, float> lexical(const QString &query, const SearchRequest &scope = {}) const;
    Photos photos() const;
    Photo photo(qint64 id) const;
    void save(const Photo &photo, const std::vector<float> &embedding,
              const QString &model = ModelVersion);
    void saveAppearance(const Photo &photo, const std::vector<float> &descriptor);
    QString saveThumbnail(const QByteArray &encoded);
    QImage thumbnail(const QString &location) const;
    void pruneThumbnails();
    void prune(const QString &folder, const QSet<QString> &seen);
    void setFavorite(qint64 id, bool value);
    void setNotes(qint64 id, const QString &notes);
    void begin();
    void commit();
    struct Catalog {
        Photos photos;
        std::vector<float> vectors;
        // One flag per photo; metadata stays visible even without a usable vector.
        std::vector<uint8_t> semanticAvailable;
        int legacyEmbeddings = 0;
        int unavailableEmbeddings = 0;
    };
    Catalog catalog(bool appearance = false, bool loadVectors = true) const;

  private:
    QSqlDatabase db_;
    QString connection_;
    std::unique_ptr<QSqlQuery> saveQuery_, appearanceQuery_, thumbnailQuery_;
    void exec(const QString &sql) const;
    QSqlQuery &prepared(std::unique_ptr<QSqlQuery> &query, const QString &sql);
};
QImage readThumbnail(const Paths &paths, const QString &location);
} // namespace piclocate
