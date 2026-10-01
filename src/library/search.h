#pragma once
#include "library/database.h"
#include <QObject>
#include <atomic>
#include <memory>

namespace piclocate {
class Inference;
class Searcher : public QObject {
    Q_OBJECT
  public:
    explicit Searcher(Paths paths);
    ~Searcher() override;
    void setLatest(quint64 generation) { latest_ = generation; }
  public slots:
    void reload();
    void updateMetadata(piclocate::Photo photo);
    void search(piclocate::SearchRequest request);
  signals:
    void results(piclocate::SearchResult result);

  private:
    Paths paths_;
    Database::Catalog catalog_;
    bool loaded_ = false;
    bool appearanceLoaded_ = false;
    bool vectorsLoaded_ = false;
    SimilarityMode loadedSimilarity_ = SimilarityMode::Subject;
    int vectorDimensions_ = Dimensions;
    std::unique_ptr<Inference> engine_;
    std::atomic<quint64> latest_{0};
    SearchRequest cachedRequest_;
    SearchResult cachedResult_;
    struct Match {
        int index;
        float score, textEvidence;
    };
    std::vector<Match> matches_;
    size_t sortedThrough_ = 0;
    bool orderRequired_ = false;
    quint64 nextCursor_ = 0;
    SearchResult pageOf(const SearchRequest &request, int offset);
    bool precedes(const Match &a, const Match &b) const;
};
} // namespace piclocate
