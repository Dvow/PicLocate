#include "library/search.h"
#include "models/appearance.h"
#include "models/inference.h"
#include <algorithm>
namespace piclocate {
Searcher::Searcher(Paths paths) : paths_(std::move(paths)) {}
Searcher::~Searcher() = default;
static bool sameSearch(const SearchRequest &a, const SearchRequest &b) {
    return a.query == b.query && a.folder == b.folder && a.referencePath == b.referencePath &&
           a.similarId == b.similarId && a.favorites == b.favorites &&
           a.orientation == b.orientation && a.mode == b.mode && a.similarity == b.similarity &&
           a.sort == b.sort && a.extension == b.extension;
}
bool Searcher::precedes(const Match &a, const Match &b) const {
    const auto &pa = catalog_.photos[a.index];
    const auto &pb = catalog_.photos[b.index];
    switch (cachedRequest_.sort) {
    case SortOrder::Newest:
    case SortOrder::Oldest:
        if (pa.modified != pb.modified)
            return cachedRequest_.sort == SortOrder::Newest ? pa.modified > pb.modified
                                                            : pa.modified < pb.modified;
        break;
    case SortOrder::Name: {
        const int comparison = pa.name.compare(pb.name, Qt::CaseInsensitive);
        if (comparison)
            return comparison < 0;
        break;
    }
    case SortOrder::Largest:
        if (qint64(pa.width) * pa.height != qint64(pb.width) * pb.height)
            return qint64(pa.width) * pa.height > qint64(pb.width) * pb.height;
        break;
    default:
        if (a.score != b.score)
            return a.score > b.score;
    }
    return a.index < b.index;
}
SearchResult Searcher::pageOf(const SearchRequest &request, int offset) {
    SearchResult page;
    page.generation = request.generation;
    page.cursor = cachedResult_.cursor;
    page.offset = offset;
    page.total = cachedResult_.total;
    page.ranked = cachedResult_.ranked;
    page.status = cachedResult_.status;
    page.error = cachedResult_.error;
    const int end = offset + qMin(qMax(0, request.limit), page.total - offset);
    if (orderRequired_ && size_t(end) > sortedThrough_) {
        const auto less = [this](const Match &a, const Match &b) { return precedes(a, b); };
        if (!sortedThrough_) {
            // Exact top page in linear selection time; no approximation of scores.
            if (size_t(end) < matches_.size())
                std::nth_element(matches_.begin(), matches_.begin() + end, matches_.end(), less);
            std::sort(matches_.begin(), matches_.begin() + end, less);
            sortedThrough_ = size_t(end);
        } else {
            // Order the remaining lightweight indices once when scrolling begins.
            // Subsequent pages only copy their own rows; inference never repeats.
            std::sort(matches_.begin() + sortedThrough_, matches_.end(), less);
            sortedThrough_ = matches_.size();
        }
    }
    page.photos.reserve(end - offset);
    const bool reference = cachedRequest_.similarId || !cachedRequest_.referencePath.isEmpty();
    for (int i = offset; i < end; ++i) {
        const auto &match = matches_[size_t(i)];
        auto photo = catalog_.photos[match.index];
        photo.score = match.score;
        if (match.textEvidence > 0)
            photo.matchReason = match.textEvidence >= .25f
                                    ? QStringLiteral("Filename / description match")
                                    : QStringLiteral("Text found in labels or image");
        else if (reference)
            photo.matchReason = cachedRequest_.similarity == SimilarityMode::Shape
                                    ? QStringLiteral("Shape and detail")
                                : cachedRequest_.similarity == SimilarityMode::Color
                                    ? QStringLiteral("Color palette")
                                : cachedRequest_.similarity == SimilarityMode::Appearance
                                    ? QStringLiteral("Appearance similarity")
                                    : QStringLiteral("Subject similarity");
        else if (page.ranked)
            photo.matchReason = QStringLiteral("Visual meaning");
        page.photos << std::move(photo);
    }
    return page;
}
void Searcher::reload() {
    cachedResult_ = {};
    matches_.clear();
    catalog_ = {};
    loaded_ = false;
    vectorsLoaded_ = false;
    // The next request selects metadata, semantic or appearance data directly.
    // Browsing after each indexing commit must not load every embedding first.
}
void Searcher::updateMetadata(Photo photo) {
    // Plain browsing and image-reference order do not depend on notes/favorites.
    // Keep that cursor and scroll position when a user edits a card. Text or
    // favorite-filtered results can change membership/rank and need a refresh.
    if (cachedRequest_.favorites || !cachedRequest_.query.trimmed().isEmpty())
        cachedResult_ = {};
    for (auto &existing : catalog_.photos)
        if (existing.id == photo.id) {
            existing.favorite = photo.favorite;
            existing.notes = photo.notes.left(700);
            break;
        }
}
void Searcher::search(SearchRequest request) {
    if (latest_ != 0 && latest_ != request.generation)
        return;
    QElapsedTimer timer;
    timer.start();
    if (request.offset > 0 && request.cursor && request.cursor == cachedResult_.cursor &&
        sameSearch(request, cachedRequest_)) {
        auto page = pageOf(request, qMin(request.offset, int(matches_.size())));
        page.elapsedMs = timer.elapsed();
        emit results(page);
        return;
    }
    // A changed query/catalog invalidates its cursor. Return a new first page
    // instead of mixing independently ranked pages or appending stale results.
    cachedResult_ = {};
    matches_.clear();
    sortedThrough_ = 0;
    SearchResult result;
    result.generation = request.generation;
    try {
        const bool reference = request.similarId || !request.referencePath.isEmpty();
        const bool appearance = reference && request.similarity != SimilarityMode::Subject;
        const bool needVectors =
            reference || (!request.query.trimmed().isEmpty() && request.mode != SearchMode::Text);
        if (!loaded_ ||
            (needVectors && (!vectorsLoaded_ || appearance != appearanceLoaded_ ||
                             (appearance && request.similarity != loadedSimilarity_)))) {
            Database db(paths_);
            catalog_ = db.catalog(appearance, needVectors);
            appearanceLoaded_ = appearance;
            vectorsLoaded_ = needVectors;
            loadedSimilarity_ = request.similarity;
            vectorDimensions_ = Dimensions;
            if (appearance && (request.similarity == SimilarityMode::Shape ||
                               request.similarity == SimilarityMode::Color)) {
                for (size_t i = 0; i < size_t(catalog_.photos.size()); ++i)
                    projectAppearance(catalog_.vectors.data() + i * Dimensions,
                                      catalog_.vectors.data() + i * 256, request.similarity);
                catalog_.vectors.resize(size_t(catalog_.photos.size()) * 256);
                catalog_.vectors.shrink_to_fit();
                vectorDimensions_ = 256;
            }
            loaded_ = true;
        }
        std::vector<float> query;
        bool semantic = false;
        if (request.similarId) {
            for (qsizetype i = 0; i < catalog_.photos.size(); ++i)
                if (catalog_.photos[i].id == request.similarId &&
                    catalog_.semanticAvailable[size_t(i)]) {
                    auto it = catalog_.vectors.begin() + i * vectorDimensions_;
                    query.assign(it, it + vectorDimensions_);
                    semantic = true;
                    break;
                }
            if (!semantic) {
                if (appearance)
                    throw std::runtime_error("Build the appearance index using Rescan > Build "
                                             "appearance index, then try Match appearance again.");
                throw std::runtime_error("Rescan this image's folder to enable Find similar. "
                                         "Browsing and text search are still available.");
            }
        } else if (!request.referencePath.isEmpty()) {
            const auto image = readImage(request.referencePath, appearance ? 512 : 0);
            if (image.isNull())
                throw std::runtime_error(
                    "The reference image could not be read. Choose another image.");
            if (appearance) {
                query = appearanceDescriptor(image);
                query.resize(projectAppearance(query.data(), query.data(), request.similarity));
            } else {
                if (!engine_)
                    engine_ = std::make_unique<Inference>(paths_.models());
                query = engine_->image(image);
                engine_->releaseVision();
            }
            semantic = true;
        } else if (!request.query.trimmed().isEmpty() && request.mode != SearchMode::Text) {
            try {
                if (!engine_)
                    engine_ = std::make_unique<Inference>(paths_.models());
                query = engine_->text(request.query);
                semantic = true;
            } catch (const std::exception &e) {
                result.status = QStringLiteral("Text match only · ") + QString::fromUtf8(e.what());
            }
        }
        const auto queryText = request.query.trimmed();
        const bool hasText = !queryText.isEmpty();
        if (latest_ != 0 && latest_ != request.generation)
            return;
        QHash<qint64, float> lexical;
        if (hasText && (request.mode != SearchMode::Visual || !semantic)) {
            Database db(paths_);
            lexical = db.lexical(request.query, request);
        }
        std::vector<Match> matches;
        matches.reserve(size_t(catalog_.photos.size()));
        for (qsizetype i = 0; i < catalog_.photos.size(); ++i) {
            if (i % 256 == 0 && latest_ != 0 && latest_ != request.generation)
                return;
            const auto &p = catalog_.photos[i];
            if (!request.extension.isEmpty() &&
                QFileInfo(p.path).suffix().compare(request.extension, Qt::CaseInsensitive) != 0)
                continue;
            if (request.favorites && !p.favorite)
                continue;
            if (!request.folder.isEmpty() && p.folder != request.folder)
                continue;
            if (p.id == request.similarId ||
                (!request.referencePath.isEmpty() && p.path == request.referencePath))
                continue;
            if (request.orientation == 1 && p.width <= p.height)
                continue;
            if (request.orientation == 2 && p.height <= p.width)
                continue;
            if (request.orientation == 3 && p.width != p.height)
                continue;
            float score = 0;
            const bool hasVector = semantic && catalog_.semanticAvailable[size_t(i)];
            if (reference && !hasVector)
                continue;
            if (hasVector)
                score = dot(catalog_.vectors.data() + size_t(i) * vectorDimensions_, query.data(),
                            vectorDimensions_);
            if (hasText) {
                score += lexical.value(p.id, 0);
                const auto dotAt = p.name.lastIndexOf(u'.');
                const auto stem = QStringView(p.name).left(dotAt < 0 ? p.name.size() : dotAt);
                if (request.mode != SearchMode::Visual &&
                    (p.name.compare(queryText, Qt::CaseInsensitive) == 0 ||
                     stem.compare(QStringView(queryText), Qt::CaseInsensitive) == 0))
                    score += .65f;
                if (!hasVector && !lexical.contains(p.id))
                    continue;
            }
            matches.push_back({int(i), score, lexical.value(p.id, 0)});
        }
        result.total = int(matches.size());
        const bool ranked = semantic || hasText;
        result.ranked = semantic;
        orderRequired_ = ranked || request.sort != SortOrder::Relevance;
        matches_ = std::move(matches);
        if (result.status.isEmpty())
            result.status =
                appearance
                    ? (request.similarity == SimilarityMode::Shape
                           ? QStringLiteral("Appearance · shape and detail · on this device")
                       : request.similarity == SimilarityMode::Color
                           ? QStringLiteral("Appearance · color palette · on this device")
                           : QStringLiteral(
                                 "Appearance · color, silhouette and edges · on this device"))
                : semantic ? (request.mode == SearchMode::Visual || reference
                                  ? QStringLiteral("Visual similarity · on this device")
                                  : QStringLiteral("Visual + text search · on this device"))
                : hasText ? QStringLiteral("Text matches · filenames, labels, OCR and descriptions")
                          : QStringLiteral("Your images, kept on this device");
        if (!appearance && semantic && catalog_.legacyEmbeddings)
            result.status += QStringLiteral(" · Earlier index supported");
        if (needVectors && catalog_.unavailableEmbeddings)
            result.status +=
                (appearance ? QStringLiteral(" · %1 images need an appearance index")
                            : QStringLiteral(" · %1 images need a rescan for visual search"))
                    .arg(catalog_.unavailableEmbeddings);
    } catch (const std::exception &e) {
        result.error = true;
        result.status = QString::fromUtf8(e.what());
    }
    result.elapsedMs = timer.elapsed();
    if (latest_ != 0 && latest_ != request.generation)
        return;
    if (!result.error) {
        result.cursor = ++nextCursor_;
        cachedRequest_ = request;
        cachedResult_ = std::move(result);
        auto page = pageOf(request, 0);
        page.elapsedMs = timer.elapsed();
        emit results(page);
    } else {
        // The UI retains its already loaded rows if a continuation fails.
        result.offset = qMax(0, request.offset);
        emit results(result);
    }
}
} // namespace piclocate
