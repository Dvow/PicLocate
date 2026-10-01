#include "app/headless.h"
#include "core/core.h"
#include "library/database.h"
#include "library/indexer.h"
#include "library/search.h"
#include <QtConcurrent>
#include <algorithm>
#include <iostream>
#include <random>

namespace piclocate {
int runHeadless(QCoreApplication &app, const QCommandLineParser &parser, const Paths &paths,
                Database &db) {
    QJsonObject report;
    int exit = 0;
    if (parser.isSet(QStringLiteral("benchmark"))) {
        constexpr int count = 40000;
        std::mt19937 random(42);
        std::uniform_real_distribution<float> distribution(-1, 1);
        std::vector<float> vectors(size_t(count) * Dimensions);
        for (auto &v : vectors)
            v = distribution(random);
        std::vector<float> query(Dimensions);
        for (auto &v : query)
            v = distribution(random);
        normalize(query);
        for (int i = 0; i < count; ++i) {
            float *row = vectors.data() + size_t(i) * Dimensions;
            const float norm = std::sqrt(dot(row, row));
            for (int j = 0; j < Dimensions; ++j)
                row[j] /= norm;
        }
        QList<double> times;
        float checksum = 0;
        for (int i = 0; i < 35; ++i) {
            QElapsedTimer timer;
            timer.start();
            auto matches = topK(vectors, query, 100);
            double ms = double(timer.nsecsElapsed()) / 1e6;
            if (i >= 5)
                times << ms;
            checksum += matches.first().second;
        }
        std::sort(times.begin(), times.end());
        report.insert(
            QStringLiteral("benchmark"),
            QJsonObject{{QStringLiteral("images"), count},
                        {QStringLiteral("dimensions"), Dimensions},
                        {QStringLiteral("vector_memory_mib"),
                         double(vectors.size() * sizeof(float)) / (1024 * 1024)},
                        {QStringLiteral("p50_ms"), times[times.size() / 2]},
                        {QStringLiteral("p95_ms"), times[int(times.size() * .95)]},
                        {QStringLiteral("checksum"), checksum},
                        {QStringLiteral("scope"),
                         QStringLiteral("Synthetic normalized float32 vectors; exact cosine + "
                                        "top-100 only. Excludes model inference, disk and UI.")}});
    }
    const auto folders = parser.values(QStringLiteral("index"));
    if (!folders.isEmpty() || parser.isSet(QStringLiteral("appearance-index"))) {
        for (const auto &folder : folders) {
            if (!QFileInfo(folder).isDir())
                throw std::runtime_error("Index path is not a directory");
            db.addFolder(folder);
        }
        QElapsedTimer timer;
        timer.start();
        // Windows OCR requires a worker apartment, including in headless mode.
        auto future =
            QtConcurrent::run([paths, connected = db.folders(),
                               appearanceOnly = parser.isSet(QStringLiteral("appearance-index")),
                               useGpu = !parser.isSet(QStringLiteral("cpu")),
                               useOcr = !parser.isSet(QStringLiteral("no-ocr"))] {
                Indexer indexer(paths);
                QJsonObject summary;
                QObject::connect(
                    &indexer, &Indexer::completed, &indexer,
                    [&](int changed, int skipped, int failed, bool cancelled,
                        const QStringList &errors) {
                        summary = {{QStringLiteral("changed"), changed},
                                   {QStringLiteral("unchanged"), skipped},
                                   {QStringLiteral("failed"), failed},
                                   {QStringLiteral("cancelled"), cancelled},
                                   {QStringLiteral("errors"), QJsonArray::fromStringList(errors)}};
                    },
                    Qt::DirectConnection);
                indexer.scan(connected, useOcr, false, appearanceOnly, useGpu);
                summary.insert(QStringLiteral("profile"), indexer.metrics());
                return summary;
            });
        const auto summary = future.result();
        report.insert(QStringLiteral("index"), summary);
        if (summary.value(QStringLiteral("failed")).toInt())
            exit = 2;
        report.insert(QStringLiteral("index_ms"), timer.elapsed());
    }
    if (parser.isSet(QStringLiteral("query")) || parser.isSet(QStringLiteral("similar")) ||
        parser.isSet(QStringLiteral("reference"))) {
        Searcher searcher(paths);
        QJsonArray searches;
        QString currentQuery;
        QObject::connect(&searcher, &Searcher::results, &app, [&](const SearchResult &r) {
            QJsonArray found;
            for (const auto &p : r.photos)
                found.append(QJsonObject{{QStringLiteral("id"), p.id},
                                         {QStringLiteral("name"), p.name},
                                         {QStringLiteral("score"), p.score},
                                         {QStringLiteral("reason"), p.matchReason},
                                         {QStringLiteral("labels"), p.tags},
                                         {QStringLiteral("ocr"), p.ocr}});
            const QJsonObject entry{{QStringLiteral("query"), currentQuery},
                                    {QStringLiteral("milliseconds"), r.elapsedMs},
                                    {QStringLiteral("status"), r.status},
                                    {QStringLiteral("results"), found}};
            report.insert(QStringLiteral("search"), entry);
            if (r.error)
                exit = 2;
            searches.append(entry);
        });
        auto queries = parser.values(QStringLiteral("query"));
        if (queries.isEmpty())
            queries << QString();
        for (const auto &query : queries) {
            currentQuery = query;
            SearchRequest request;
            request.query = query;
            request.similarId = parser.value(QStringLiteral("similar")).toLongLong();
            if (parser.isSet(QStringLiteral("reference")))
                request.referencePath = normalizedPath(parser.value(QStringLiteral("reference")));
            request.similarity = parser.isSet(QStringLiteral("appearance"))
                                     ? SimilarityMode::Appearance
                                     : SimilarityMode::Subject;
            if (parser.isSet(QStringLiteral("shape-match")))
                request.similarity = SimilarityMode::Shape;
            if (parser.isSet(QStringLiteral("color-match")))
                request.similarity = SimilarityMode::Color;
            if (parser.isSet(QStringLiteral("text-only")))
                request.mode = SearchMode::Text;
            searcher.search(request);
        }
        report.insert(QStringLiteral("searches"), searches);
    }
    report.insert(QStringLiteral("library_images"), db.count());
    const auto json = QJsonDocument(report).toJson(QJsonDocument::Indented);
    if (parser.isSet(QStringLiteral("report"))) {
        QSaveFile file(parser.value(QStringLiteral("report")));
        if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
            throw std::runtime_error("Could not write report");
    } else
        std::cout << json.constData() << std::endl;
    return exit;
}
} // namespace piclocate
