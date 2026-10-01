#pragma once
#include <QList>
#include <QMetaType>
#include <QString>

namespace piclocate {
inline constexpr int Dimensions = 512;
inline const QString ModelVersion = QStringLiteral("clip-b32-int8-d15189d7-ort130-v3");
inline const QString GpuModelVersion = QStringLiteral("clip-b32-fp16-d15189d7-dml124-v3");
// Same checkpoint and space. v3 fixes hidden RGB and cropped transparent assets.
inline const QString CompatiblePreviousModelVersion =
    QStringLiteral("clip-b32-int8-d15189d7-ort130-v2");
inline const QString CompatibleLegacyModelVersion = QStringLiteral("clip-b32-int8-d15189d7-v1");
struct Photo {
    qint64 id = 0;
    QString path, folder, name, thumb, tags, ocr, notes, matchReason;
    int width = 0, height = 0;
    qint64 bytes = 0, modified = 0;
    bool favorite = false;
    float score = 0;
};
using Photos = QList<Photo>;
enum class SearchMode { Hybrid, Visual, Text };
enum class SortOrder { Relevance, Newest, Oldest, Name, Largest };
enum class SimilarityMode { Subject, Appearance, Shape, Color };
struct SearchRequest {
    quint64 generation = 0;
    QString query, folder, referencePath;
    qint64 similarId = 0;
    bool favorites = false;
    int orientation = 0; // 0 all, 1 landscape, 2 portrait, 3 square
    int limit = 300, offset = 0;
    quint64 cursor = 0; // Returned search snapshot; zero starts a fresh search.
    SearchMode mode = SearchMode::Hybrid;
    SimilarityMode similarity = SimilarityMode::Subject;
    SortOrder sort = SortOrder::Relevance;
    QString extension;
};
struct SearchResult {
    quint64 generation = 0;
    quint64 cursor = 0;
    int offset = 0;
    Photos photos;
    qint64 elapsedMs = 0;
    QString status;
    int total = 0;
    bool ranked = false, error = false;
};
struct Paths {
    QString root;
    QString database() const { return root + QStringLiteral("/library.sqlite"); }
    QString thumbnails() const { return root + QStringLiteral("/thumbnails"); }
    QString models() const;
    void create() const;
};
} // namespace piclocate
Q_DECLARE_METATYPE(piclocate::Photo)
Q_DECLARE_METATYPE(piclocate::Photos)
Q_DECLARE_METATYPE(piclocate::SearchRequest)
Q_DECLARE_METATYPE(piclocate::SearchResult)
