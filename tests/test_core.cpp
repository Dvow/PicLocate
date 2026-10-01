#include "core/core.h"
#include "library/database.h"
#include "library/indexer.h"
#include "library/search.h"
#include "models/appearance.h"
#include "models/inference.h"
#include "models/model_download.h"
#include <QtConcurrent>
#include <QtTest>
#include <limits>
using namespace piclocate;
class CoreTests : public QObject {
    Q_OBJECT
    static bool editFixture(const Paths &paths, const QString &sql,
                            const QVariantList &values = {}) {
        const auto connection = QUuid::createUuid().toString();
        bool ok = false;
        {
            auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            database.setDatabaseName(paths.database());
            if (database.open()) {
                QSqlQuery query(database);
                query.prepare(sql);
                for (const auto &value : values)
                    query.addBindValue(value);
                ok = query.exec();
            }
        }
        QSqlDatabase::removeDatabase(connection);
        return ok;
    }
  private slots:
    void extremeAspectRatioUsesBoundedCrop() {
        QImage image(1, 10000000, QImage::Format_RGB32);
        QVERIFY(!image.isNull());
        image.fill(Qt::green);
        QImage expected(224, 224, QImage::Format_RGB32);
        expected.fill(Qt::green);
        // Its virtual resized height exceeds INT_MAX; allocating every source
        // row's intermediate crop would also require more than six GiB.
        QCOMPARE(Inference::preprocess(image), Inference::preprocess(expected));
    }
    void textEvidenceMatchesColumnScopedFts() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        std::vector<float> vector(Dimensions, 0.f);
        vector[0] = 1.f;
        for (int i = 0; i < 5; ++i) {
            Photo photo;
            photo.folder = directory.path();
            photo.name = i == 0 ? QStringLiteral("RedSwordIcon.png")
                                : QStringLiteral("opaque-%1.png").arg(i);
            photo.path = directory.path() + u'/' + photo.name;
            photo.tags = i == 1 || i == 4 ? QStringLiteral("red sword") : QStringLiteral("");
            photo.ocr = i == 2 ? QStringLiteral("red sword") : QStringLiteral("");
            database.save(photo, vector);
            database.setNotes(database.photos().first().id, i == 3   ? QStringLiteral("red sword")
                                                            : i == 4 ? QStringLiteral("red")
                                                                     : QStringLiteral(""));
        }
        SearchRequest request;
        request.mode = SearchMode::Text;
        for (const auto &expression :
             {QStringLiteral("\"red\""), QStringLiteral("\"swor\"*"),
              QStringLiteral("\"red sword\""), QStringLiteral("\"red\" AND \"sword\"")}) {
            QVERIFY(
                editFixture(paths,
                            QStringLiteral("UPDATE photos SET favorite=id IN (SELECT rowid FROM "
                                           "photo_text WHERE photo_text MATCH ?)"),
                            {QStringLiteral("{name search_name notes}: (") + expression + u')'}));
            const auto input = expression == QStringLiteral("\"red\" AND \"sword\"")
                                   ? QStringLiteral("red sword")
                               : expression == QStringLiteral("\"swor\"*") ? QStringLiteral("swor*")
                                                                           : expression;
            const auto matches = database.lexical(input, request);
            for (const auto &photo : database.photos())
                QCOMPARE(matches.value(photo.id, 0.f) >= .25f, photo.favorite);
        }
    }
    void metadataStaysWritableBetweenInferenceBatches() {
        if (qEnvironmentVariable("PICLOCATE_MODEL_DIR").isEmpty())
            QSKIP("Set PICLOCATE_MODEL_DIR for the concurrent indexing regression");
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        for (int i = 0; i < 17; ++i) {
            QImage image(24, 24, QImage::Format_RGB32);
            image.fill(QColor(i * 13, i * 7, i * 3));
            QVERIFY(image.save(directory.path() + QStringLiteral("/asset-%1.png").arg(i)));
        }
        Indexer indexer(paths);
        bool attempted = false, saved = false;
        qint64 elapsed = -1;
        connect(&indexer, &Indexer::backendChanged, this, [&](const auto &) {
            if (attempted || !database.count())
                return;
            attempted = true;
            // A different connection/thread models a UI edit while the indexer
            // is between inference batches. Its writer lock must be released.
            const auto result =
                QtConcurrent::run([paths] {
                    QElapsedTimer timer;
                    timer.start();
                    try {
                        Database editor(paths);
                        const auto id = editor.photos().first().id;
                        editor.setFavorite(id, true);
                        editor.setNotes(id, QStringLiteral("Edited during indexing"));
                        return QPair<bool, qint64>{true, timer.elapsed()};
                    } catch (...) {
                        return QPair<bool, qint64>{false, timer.elapsed()};
                    }
                }).result();
            saved = result.first;
            elapsed = result.second;
        });
        QSignalSpy completed(&indexer, &Indexer::completed);
        indexer.scan({directory.path()}, false, false, false, false);
        QCOMPARE(completed.size(), 1);
        QCOMPARE(completed.first()[2].toInt(), 0);
        QVERIFY(attempted);
        QVERIFY(saved);
        QVERIFY2(elapsed < 1000,
                 qPrintable(QStringLiteral("Metadata writer waited %1 ms").arg(elapsed)));
        QCOMPARE(database.count(), 17);
        int edited = 0;
        for (const auto &photo : database.photos())
            edited += photo.favorite && photo.notes == QStringLiteral("Edited during indexing");
        QCOMPARE(edited, 1);
    }
    void filenameWordsAndTransactionalUpgrade() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        Photo photo;
        photo.folder = directory.path();
        photo.name = QStringLiteral("XMLFireSwordIcon02.png");
        photo.path = directory.path() + u'/' + photo.name;
        photo.width = photo.height = 32;
        photo.bytes = 100;
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::red);
        QByteArray encoded;
        QBuffer buffer(&encoded);
        QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(image.save(&buffer, "PNG"));
        photo.thumb = database.saveThumbnail(encoded);
        std::vector<float> vector(Dimensions, 0.f);
        vector[17] = 1.f;
        database.save(photo, vector);
        database.saveAppearance(photo, appearanceDescriptor(image));
        const auto id = database.photos().first().id;
        database.setFavorite(id, true);
        database.setNotes(id, QStringLiteral("Handmade weapon, keep this description"));
        const auto before = database.catalog();
        // Recreate the former on-disk schema, including its external-content FTS.
        for (const auto &sql :
             {QStringLiteral("DROP TRIGGER photos_ai"), QStringLiteral("DROP TRIGGER photos_ad"),
              QStringLiteral("DROP TRIGGER photos_au"), QStringLiteral("DROP TABLE photo_text"),
              QStringLiteral("ALTER TABLE photos DROP COLUMN search_name"),
              QStringLiteral("CREATE VIRTUAL TABLE photo_text USING "
                             "fts5(name,tags,ocr,notes,content='photos',content_rowid='id')"),
              QStringLiteral("INSERT INTO photo_text(photo_text) VALUES('rebuild')"),
              QStringLiteral("PRAGMA user_version=3")})
            QVERIFY(editFixture(paths, sql));
        database.initialize();
        QCOMPARE(database.catalog().vectors, before.vectors);
        QCOMPARE(database.photo(id).notes, before.photos.first().notes);
        QVERIFY(database.photo(id).favorite);
        QCOMPARE(database.photo(id).thumb, photo.thumb);
        QCOMPARE(database.thumbnail(photo.thumb).size(), image.size());
        QCOMPARE(database.stats().appearances, 1);
        SearchRequest scope;
        scope.mode = SearchMode::Text;
        for (const auto &query : {QStringLiteral("fire sword"), QStringLiteral("xml icon 02"),
                                  QStringLiteral("\"fire sword\""), QStringLiteral("swor*"),
                                  QStringLiteral("XMLFireSwordIcon02")})
            QVERIFY2(database.lexical(query, scope).contains(id), qPrintable(query));
        QVERIFY(database.lexical(QStringLiteral("fire potion"), scope).isEmpty());
        database.initialize(); // Repeated startup must leave the migrated index intact.
        database.setNotes(id, QStringLiteral("New description"));
        QVERIFY(database.lexical(QStringLiteral("new description"), scope).contains(id));
        QVERIFY(database.lexical(QStringLiteral("handmade"), scope).isEmpty());
        photo.name = QStringLiteral("ÉpéeBleue12.png");
        database.save(photo, vector); // Same file ID; prepared statement must rebind every value.
        QVERIFY(database.lexical(QStringLiteral("epee bleue 12"), scope).contains(id));
        QVERIFY(database.lexical(QStringLiteral("fire sword"), scope).isEmpty());
        QCOMPARE(database.photo(id).notes, QStringLiteral("New description"));
        QVERIFY(database.photo(id).favorite);
        QVERIFY(editFixture(paths, QStringLiteral("PRAGMA user_version=99")));
        QVERIFY_EXCEPTION_THROWN(database.initialize(), std::runtime_error);
        QCOMPARE(database.count(), 1);
    }
    void paginatedOrderMatchesCompleteReference() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        database.begin();
        for (int i = 0; i < 529; ++i) {
            Photo photo;
            photo.folder = directory.path();
            photo.name = QStringLiteral("asset-%1.png").arg((i * 137) % 529, 3, 10, QChar(u'0'));
            photo.path = directory.path() + u'/' + photo.name;
            photo.modified = i % 11;
            photo.width = 16 + i % 7;
            photo.height = 16 + i % 13;
            photo.bytes = i % 3;
            std::vector<float> vector(Dimensions, 0.f);
            vector[size_t(i % 3)] = 1.f;
            database.save(photo, vector);
        }
        database.commit();
        const auto original = database.photos();
        for (const auto order : {SortOrder::Relevance, SortOrder::Newest, SortOrder::Oldest,
                                 SortOrder::Name, SortOrder::Largest}) {
            QList<int> expected;
            for (int i = 0; i < original.size(); ++i)
                if (original[i].id != 1)
                    expected.append(i);
            std::sort(expected.begin(), expected.end(), [&](int a, int b) {
                const auto &pa = original[a];
                const auto &pb = original[b];
                if (order == SortOrder::Relevance && (pa.bytes == 0) != (pb.bytes == 0))
                    return pa.bytes == 0;
                if ((order == SortOrder::Newest || order == SortOrder::Oldest) &&
                    pa.modified != pb.modified)
                    return order == SortOrder::Newest ? pa.modified > pb.modified
                                                      : pa.modified < pb.modified;
                if (order == SortOrder::Name && pa.name != pb.name)
                    return pa.name < pb.name;
                if (order == SortOrder::Largest && pa.width * pa.height != pb.width * pb.height)
                    return pa.width * pa.height > pb.width * pb.height;
                return a < b;
            });
            Searcher searcher(paths);
            SearchResult result;
            connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
            SearchRequest request;
            request.similarId = 1;
            request.sort = order;
            request.limit = 17;
            for (int offset = 0; offset < expected.size(); offset += request.limit) {
                request.offset = offset;
                searcher.search(request);
                QVERIFY(!result.error);
                QCOMPARE(result.total, expected.size());
                QCOMPARE(result.offset, offset);
                for (int i = 0; i < result.photos.size(); ++i) {
                    const auto &photo = original[expected[offset + i]];
                    QCOMPARE(result.photos[i].id, photo.id);
                    QCOMPARE(result.photos[i].score, photo.bytes == 0 ? 1.f : 0.f);
                }
                request.cursor = result.cursor;
            }
            request.offset = 900;
            request.limit = std::numeric_limits<int>::max();
            searcher.search(request);
            QVERIFY(result.photos.isEmpty());
            QCOMPARE(result.offset, expected.size());
        }
        QVERIFY(database.catalog(false, false).vectors.empty());
    }
    void focusedProjectionPreservesCosine() {
        quint32 random = 77;
        for (int sample = 0; sample < 101; ++sample) {
            std::vector<float> a(Dimensions), b(Dimensions), pa(256), pb(256);
            for (int i = 0; i < Dimensions; ++i) {
                random = random * 1664525 + 1013904223;
                a[size_t(i)] = sample == 100 ? 0 : float(random % 1024) / 512.f - 1;
                random = random * 1664525 + 1013904223;
                b[size_t(i)] = float(random % 1024) / 512.f - 1;
            }
            for (const auto mode : {SimilarityMode::Shape, SimilarityMode::Color}) {
                projectAppearance(a.data(), pa.data(), mode);
                projectAppearance(b.data(), pb.data(), mode);
                const auto actual = dot(pa.data(), pb.data(), 256);
                const auto expected = appearanceSimilarity(a.data(), b.data(), mode);
                QVERIFY(std::abs(actual - expected) < 1e-6f);
                auto inplace = a;
                inplace.resize(projectAppearance(inplace.data(), inplace.data(), mode));
                QCOMPARE(inplace, pa);
            }
        }
    }
    void centeredCropPreservesExistingTensors() {
        const char *digests[] = {
            "e3f3d75f5ac33fb38cac7643eaa87ee68df4561e5bb9d6a8c6ec3a3bfb870923",
            "7a2cd40d5ec5730720fafc014d82ff0c0341f20818c1a3f2491a810ce2ea92d7",
            "166c581f1341e5c673829f60d0d2f69883d7ab403792ef03de01a7e3a0d6d666",
            "9b758de9815bc3053a6ea4d06fcba25912985237d7272d57e7647bb4f758ec19",
            "6df8ede7847a6df126344da0ac24c7761f02287b258709d4a0a891a7cc5350a8"};
        int index = 0;
        for (const auto size : {QSize(224, 4096), QSize(4096, 224), QSize(301, 233),
                                QSize(233, 301), QSize(17, 4097)}) {
            QImage image(size, QImage::Format_RGB32);
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x)
                    image.setPixel(x, y,
                                   qRgb((x * 17 + y * 13) % 256, (x * 3 + y * 19) % 256,
                                        (x * 11 + y * 7) % 256));
            const auto pixels = Inference::preprocess(image);
            const auto digest = QCryptographicHash::hash(
                                    QByteArrayView(reinterpret_cast<const char *>(pixels.data()),
                                                   pixels.size() * sizeof(float)),
                                    QCryptographicHash::Sha256)
                                    .toHex();
            QCOMPARE(digest, QByteArray(digests[index++]));
        }
    }
    void searchPerformanceEvidence() {
        const auto directory =
            QDir::fromNativeSeparators(qEnvironmentVariable("PICLOCATE_SEARCH_BENCHMARK"));
        if (directory.isEmpty())
            QSKIP("Set PICLOCATE_SEARCH_BENCHMARK to an isolated benchmark directory");
        Paths paths{directory};
        paths.create();
        Database database(paths);
        database.initialize();
        if (!database.count()) {
            database.addFolder(directory);
            quint32 random = 1234567;
            auto next = [&] {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                return float(random % 65536) / 65536.f - .5f;
            };
            database.begin();
            for (int i = 0; i < 40000; ++i) {
                Photo photo;
                photo.folder = directory;
                photo.name = QStringLiteral("asset-%1.png").arg(i, 5, 10, QChar(u'0'));
                photo.path = directory + u'/' + photo.name;
                photo.modified = i % 31;
                photo.width = 16 + i % 257;
                photo.height = 16 + i % 131;
                std::vector<float> vector(Dimensions), appearance(Dimensions);
                for (auto &value : vector)
                    value = next();
                for (auto &value : appearance)
                    value = next();
                normalize(vector);
                normalize(appearance);
                database.save(photo, vector);
                database.saveAppearance(photo, appearance);
            }
            database.commit();
        }
        QJsonObject report;
        for (int scenario = 0; scenario < 6; ++scenario) {
            Searcher searcher(paths);
            SearchResult result;
            connect(&searcher, &Searcher::results, this,
                    [&](const SearchResult &value) { result = value; });
            SearchRequest request;
            request.limit = 300;
            if (scenario == 1) {
                request.mode = SearchMode::Text;
                request.query = QStringLiteral("asset");
            } else if (scenario >= 2) {
                request.similarId = 17000;
                request.similarity = scenario == 2   ? SimilarityMode::Subject
                                     : scenario == 3 ? SimilarityMode::Appearance
                                     : scenario == 4 ? SimilarityMode::Shape
                                                     : SimilarityMode::Color;
            }
            QElapsedTimer timer;
            timer.start();
            searcher.search(request);
            const auto cold = timer.nsecsElapsed();
            QVERIFY2(!result.error, qPrintable(result.status));
            std::vector<qint64> samples;
            for (int i = 0; i < 15; ++i) {
                timer.restart();
                searcher.search(request);
                samples.push_back(timer.nsecsElapsed());
            }
            std::sort(samples.begin(), samples.end());
            QJsonArray ids;
            for (const auto &photo : result.photos)
                ids.append(photo.id);
            const auto cursor = result.cursor;
            request.offset = 300;
            request.cursor = cursor;
            timer.restart();
            searcher.search(request);
            const auto pageTime = timer.nsecsElapsed();
            QCOMPARE(result.offset, 300);
            report.insert(QString::number(scenario),
                          QJsonObject{{QStringLiteral("cold_ms"), cold / 1e6},
                                      {QStringLiteral("warm_median_ms"), samples[7] / 1e6},
                                      {QStringLiteral("page_ms"), pageTime / 1e6},
                                      {QStringLiteral("total"), result.total},
                                      {QStringLiteral("first_ids"), ids}});
        }
        QJsonArray preprocessing;
        for (const auto size : {QSize(224, 4096), QSize(4096, 224), QSize(301, 233),
                                QSize(233, 301), QSize(17, 4097)}) {
            QImage image(size, QImage::Format_RGB32);
            for (int y = 0; y < size.height(); ++y)
                for (int x = 0; x < size.width(); ++x)
                    image.setPixel(x, y,
                                   qRgb((x * 17 + y * 13) % 256, (x * 3 + y * 19) % 256,
                                        (x * 11 + y * 7) % 256));
            QElapsedTimer timer;
            timer.start();
            std::vector<float> pixels;
            for (int i = 0; i < 5; ++i)
                pixels = Inference::preprocess(image);
            const auto digest = QCryptographicHash::hash(
                QByteArrayView(reinterpret_cast<const char *>(pixels.data()),
                               pixels.size() * sizeof(float)),
                QCryptographicHash::Sha256);
            preprocessing.append(
                QJsonObject{{QStringLiteral("width"), size.width()},
                            {QStringLiteral("height"), size.height()},
                            {QStringLiteral("mean_ms"), timer.nsecsElapsed() / 5e6},
                            {QStringLiteral("sha256"), QString::fromLatin1(digest.toHex())}});
        }
        report.insert(QStringLiteral("preprocessing"), preprocessing);
        QFile output(qEnvironmentVariable("PICLOCATE_BENCHMARK_REPORT",
                                          directory + QStringLiteral("/search-benchmark.json")));
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(report).toJson());
    }
    void vectorBackends() {
        if (qEnvironmentVariableIsSet("PICLOCATE_DISABLE_AVX2"))
            QCOMPARE(vectorBackend(), QStringLiteral("portable CPU"));
        for (const int size : {1, 7, 31, 32, 129, 512, 513}) {
            std::vector<float> a(size), b(size);
            double expected = 0;
            for (int i = 0; i < size; ++i) {
                a[size_t(i)] = float(i % 17 - 8) / 19;
                b[size_t(i)] = float(i % 13 - 6) / 11;
                expected += double(a[size_t(i)]) * b[size_t(i)];
            }
            QVERIFY(std::abs(double(dot(a.data(), b.data(), size)) - expected) < 0.00002);
        }
    }
    void paginationReusesRankingAndRejectsStaleCursors() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        QImage image(32, 32, QImage::Format_RGB32);
        image.fill(Qt::red);
        const auto reference = directory.path() + QStringLiteral("/reference.png");
        QVERIFY(image.save(reference));
        const auto descriptor = appearanceDescriptor(image);
        std::vector<float> vector(Dimensions, 0.f);
        vector[0] = 1.f;
        database.begin();
        for (int i = 0; i < 45; ++i) {
            Photo photo;
            photo.folder = directory.path();
            photo.name = QStringLiteral("asset-%1.png").arg(i, 3, 10, QChar(u'0'));
            photo.path = directory.path() + u'/' + photo.name;
            database.save(photo, vector);
            database.saveAppearance(photo, descriptor);
        }
        database.commit();
        Searcher searcher(paths);
        SearchResult result;
        int emissions = 0;
        connect(&searcher, &Searcher::results, this, [&](const auto &value) {
            result = value;
            ++emissions;
        });
        SearchRequest request;
        request.referencePath = reference;
        request.similarity = SimilarityMode::Appearance;
        request.sort = SortOrder::Name;
        request.limit = 13;
        searcher.search(request);
        QVERIFY(!result.error);
        QCOMPARE(result.total, 45);
        QCOMPARE(result.photos.size(), 13);
        QCOMPARE(result.offset, 0);
        QVERIFY(result.cursor);
        const auto cursor = result.cursor;
        QSet<qint64> ids;
        for (const auto &photo : result.photos)
            ids.insert(photo.id);
        // Continuations use the original search snapshot, with no second
        // image decode/inference/rank, even if the reference is now unavailable.
        QVERIFY(QFile::remove(reference));
        for (int offset : {13, 26, 39}) {
            request.offset = offset;
            request.cursor = cursor;
            searcher.search(request);
            QVERIFY(!result.error);
            QCOMPARE(result.cursor, cursor);
            QCOMPARE(result.offset, offset);
            QCOMPARE(result.photos.size(), qMin(13, 45 - offset));
            QCOMPARE(result.photos.first().name,
                     QStringLiteral("asset-%1.png").arg(offset, 3, 10, QChar(u'0')));
            for (const auto &photo : result.photos) {
                QVERIFY(!ids.contains(photo.id));
                ids.insert(photo.id);
            }
        }
        QCOMPARE(ids.size(), 45);
        request.offset = 45;
        searcher.search(request);
        QVERIFY(result.photos.isEmpty());
        QCOMPARE(result.total, 45);
        QVERIFY(image.save(reference));
        const auto photo = database.photos().first();
        database.setFavorite(photo.id, true);
        searcher.updateMetadata(database.photo(photo.id));
        request.offset = 39;
        searcher.search(request);
        QCOMPARE(result.offset, 39); // A favorite edit cannot reorder an unfiltered image search.
        QCOMPARE(result.cursor, cursor);
        QCOMPARE(result.photos.last().id, photo.id);
        QVERIFY(result.photos.last().favorite);
        request.cursor = result.cursor;
        request.favorites = true;
        searcher.search(request);
        QCOMPARE(result.offset, 0); // Different filters cannot reuse the previous cursor.
        QCOMPARE(result.total, 1);
        QCOMPARE(result.photos.first().id, photo.id);
        request.cursor = result.cursor;
        request.offset = 1;
        database.setFavorite(photo.id, false);
        searcher.updateMetadata(database.photo(photo.id));
        searcher.search(request);
        QCOMPARE(result.offset, 0); // Favorite-filter membership changed; restart safely.
        QCOMPARE(result.total, 0);
        const int before = emissions;
        searcher.setLatest(100);
        request.generation = 99;
        searcher.search(request);
        QCOMPARE(emissions, before);
    }
    void fileIntegrityUsesCompleteContents() {
        QTemporaryDir directory;
        const auto path = directory.path() + QStringLiteral("/model.bin");
        QVERIFY(fileSha256(path).isEmpty());
        auto write = [&](const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly | QIODevice::Truncate) &&
                   file.write(bytes) == bytes.size();
        };
        QVERIFY(write({}));
        QCOMPARE(fileSha256(path).toHex(),
                 QByteArray("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        QVERIFY(write(QByteArray("abc")));
        QCOMPARE(fileSha256(path).toHex(),
                 QByteArray("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        QByteArray bytes(2 * 1024 * 1024 + 113, Qt::Uninitialized);
        for (qsizetype i = 0; i < bytes.size(); ++i)
            bytes[i] = char(i % 251);
        QVERIFY(write(bytes));
        const auto before = fileSha256(path);
        QCOMPARE(before, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
        const auto modified = QFileInfo(path).lastModified();
        bytes[bytes.size() - 1] ^= char(1);
        QVERIFY(write(bytes));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
        file.close();
        // Same size and timestamp cannot bypass verification after a mutation.
        const auto after = fileSha256(path);
        QVERIFY(after != before);
        QCOMPARE(after, QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
    }
    void similarityIntentAndExternalReference() {
        QTemporaryDir directory;
        Paths paths{directory.path() + QStringLiteral("/library")};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        auto make = [](QColor color, bool circle) {
            QImage image(128, 128, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.setPen(Qt::NoPen);
            painter.setBrush(color);
            if (circle)
                painter.drawEllipse(20, 20, 88, 88);
            else
                painter.drawRect(48, 8, 32, 112);
            return image;
        };
        const auto red = make(Qt::red, false), blue = make(Qt::blue, false),
                   circle = make(Qt::red, true);
        const auto a = appearanceDescriptor(red), b = appearanceDescriptor(blue),
                   c = appearanceDescriptor(circle);
        QVERIFY(appearanceSimilarity(a.data(), b.data(), SimilarityMode::Shape) >
                appearanceSimilarity(a.data(), c.data(), SimilarityMode::Shape));
        QVERIFY(appearanceSimilarity(a.data(), c.data(), SimilarityMode::Color) >
                appearanceSimilarity(a.data(), b.data(), SimilarityMode::Color));
        std::vector<float> vector(Dimensions, 0.f);
        vector[0] = 1;
        const auto reference = directory.path() + QStringLiteral("/reference.png");
        QVERIFY(red.save(reference));
        int i = 0;
        for (const auto &image : {blue, circle}) {
            Photo p;
            p.path = directory.path() + QStringLiteral("/%1.png").arg(i++);
            p.name = QFileInfo(p.path).fileName();
            p.folder = directory.path();
            p.bytes = 12;
            database.save(p, vector);
            database.saveAppearance(p, appearanceDescriptor(image));
        }
        Searcher searcher(paths);
        SearchResult result;
        connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
        SearchRequest request;
        request.referencePath = reference;
        request.similarity = SimilarityMode::Shape;
        searcher.search(request);
        QVERIFY(!result.error);
        QCOMPARE(result.photos.size(), 2);
        QCOMPARE(result.photos.first().name, QStringLiteral("0.png"));
        request.similarity = SimilarityMode::Color;
        searcher.search(request);
        QCOMPARE(result.photos.first().name, QStringLiteral("1.png"));
        const auto stats = database.stats();
        QCOMPARE(stats.images, database.count());
        QCOMPARE(stats.appearances, database.stats().appearances);
        QCOMPARE(stats.bytes, qint64(24));
        const auto entries = database.scanEntries();
        QCOMPARE(entries.size(), 2);
        for (const auto &entry : entries)
            QVERIFY(entry.embeddingCurrent && entry.appearanceCurrent);
        request.referencePath += QStringLiteral(".missing");
        searcher.search(request);
        QVERIFY(result.error);
        QVERIFY(result.photos.isEmpty());
        QCOMPARE(database.count(), 2); // A reference is never imported into the library.
    }
    void explicitMetadataOutranksGeneratedLabels() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database db(paths);
        db.initialize();
        db.addFolder(directory.path());
        std::vector<float> vector(Dimensions, 0.f);
        vector[0] = 1;
        Photo photo;
        photo.folder = directory.path();
        photo.name = QStringLiteral("red_sword.png");
        photo.path = directory.path() + u'/' + photo.name;
        db.save(photo, vector);
        const auto namedId = db.photos().first().id;
        photo.name = QStringLiteral("00042.png");
        photo.path = directory.path() + u'/' + photo.name;
        photo.tags = QStringLiteral("red sword");
        db.save(photo, vector);
        const auto generatedId = db.photos().first().id;
        Searcher searcher(paths);
        SearchResult result;
        connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
        SearchRequest request;
        request.mode = SearchMode::Text;
        request.query = QStringLiteral("red sword");
        searcher.search(request);
        QCOMPARE(result.photos.size(), 2);
        QCOMPARE(result.photos.first().id, namedId);
        QVERIFY(result.photos.first().score > result.photos.last().score);
        db.setNotes(generatedId, QStringLiteral("red sword"));
        const auto weights = db.lexical(request.query, request);
        QVERIFY(weights.value(generatedId) >= .25f);
        request.query = QStringLiteral("red_sword.png");
        searcher.search(request);
        QCOMPARE(result.photos.first().id, namedId);
        QVERIFY(result.photos.first().score > .65f);
    }
    void transactionalThumbnailsAndDeduplication() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        QImage image(96, 80, QImage::Format_ARGB32);
        image.fill(QColor(20, 150, 60, 120));
        QByteArray encoded;
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
        database.begin();
        const auto key = database.saveThumbnail(encoded);
        QVERIFY(key.startsWith(QStringLiteral("piclocate-thumb:")));
        QCOMPARE(database.saveThumbnail(encoded), key);
        Photo photo;
        photo.folder = directory.path();
        photo.path = directory.path() + QStringLiteral("/one.png");
        photo.thumb = key;
        std::vector<float> vector(Dimensions, 0);
        vector[0] = 1;
        database.save(photo, vector);
        database.commit();
        const auto decoded = readThumbnail(paths, key);
        QCOMPARE(decoded.size(), image.size());
        QCOMPARE(decoded.pixel(40, 40), image.pixel(40, 40));
        QCOMPARE(QDir(paths.thumbnails()).entryList(QDir::Files).size(), 0);
        database.pruneThumbnails();
        QVERIFY(!database.thumbnail(key).isNull());
        database.removeFolder(directory.path());
        QVERIFY(database.thumbnail(key).isNull());
    }
    void gpuBatchMatchesIndependentInference() {
        const auto models = qEnvironmentVariable("PICLOCATE_MODEL_DIR");
        if (models.isEmpty() || !QFileInfo::exists(models + u'/' + gpuModelFile().name))
            QSKIP("Optional GPU model is not installed");
        Inference inference(models);
        inference.enableGpu();
        if (!inference.usesGpu())
            QSKIP(qPrintable(inference.gpuFailure()));
        std::vector<std::vector<float>> pixels;
        for (const auto &color : {Qt::red, Qt::green, Qt::blue}) {
            QImage image(128, 128, QImage::Format_RGB32);
            image.fill(color);
            pixels.push_back(Inference::preprocess(image));
        }
        const auto batch = inference.imageBatch(pixels);
        QCOMPARE(batch.size(), size_t(3));
        QCOMPARE(inference.embeddingModel(), GpuModelVersion);
        for (size_t i = 0; i < pixels.size(); ++i) {
            std::vector<std::vector<float>> one{pixels[i]};
            const auto independent = inference.imageBatch(one);
            QVERIFY(dot(batch[i].data(), independent[0].data()) > .9999f);
            const auto cpu = inference.imagePixels(pixels[i]);
            QVERIFY(dot(cpu.data(), batch[i].data()) > .90f);
            QVERIFY(std::abs(dot(batch[i].data(), batch[i].data()) - 1.f) < 1e-5f);
        }
        // Padding the partial batch cannot mix images or make the output order unstable.
        std::reverse(pixels.begin(), pixels.end());
        const auto reverse = inference.imageBatch(pixels);
        for (size_t i = 0; i < batch.size(); ++i)
            QVERIFY(dot(batch[i].data(), reverse[batch.size() - i - 1].data()) > .9999f);
        inference.releaseVision();
        QVERIFY(!inference.usesGpu());
        QCOMPARE(inference.embeddingModel(), ModelVersion);
        std::vector<std::vector<float>> one{pixels.front()};
        QVERIFY(dot(inference.imageBatch(one).front().data(), batch.back().data()) > .90f);
    }
    void identicalInputsReuseEmbeddingsAndKeepRecords() {
        if (qEnvironmentVariable("PICLOCATE_MODEL_DIR").isEmpty())
            QSKIP("Set PICLOCATE_MODEL_DIR for indexing");
        QTemporaryDir directory;
        Paths paths{directory.path() + QStringLiteral("/data")};
        paths.create();
        const auto folder = directory.path() + QStringLiteral("/images");
        QVERIFY(QDir().mkpath(folder));
        for (int i = 0; i < 3; ++i) {
            QImage image(96, 96, QImage::Format_RGB32);
            image.fill(i < 2 ? Qt::red : Qt::blue);
            QVERIFY(image.save(folder + QStringLiteral("/%1.png").arg(i)));
        }
        Database database(paths);
        database.initialize();
        database.addFolder(folder);
        Indexer indexer(paths);
        indexer.scan({folder}, false, false, false, false);
        QCOMPARE(database.count(), 3);
        QCOMPARE(indexer.metrics().value(QStringLiteral("reused_embeddings")).toInt(), 1);
        QCOMPARE(indexer.metrics().value(QStringLiteral("inference_images")).toInt(), 2);
        const auto first = database.photos().first();
        database.setFavorite(first.id, true);
        database.setNotes(first.id, QStringLiteral("Independent record"));
        indexer.scan({folder}, false, true, false, false);
        QCOMPARE(database.count(), 3);
        QVERIFY(database.photo(first.id).favorite);
        QCOMPARE(database.photo(first.id).notes, QStringLiteral("Independent record"));
    }
    void gpuModelCatalogCompatibility() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        Photo photo;
        photo.folder = directory.path();
        photo.path = directory.path() + QStringLiteral("/gpu.png");
        std::vector<float> vector(Dimensions, 0);
        vector[0] = 1;
        database.save(photo, vector, GpuModelVersion);
        QCOMPARE(database.stats().outdated, 0);
        QVERIFY(database.scanEntries().value(photo.path).embeddingCurrent);
        const auto catalog = database.catalog();
        QCOMPARE(catalog.semanticAvailable[0], uint8_t(1));
        QCOMPARE(catalog.legacyEmbeddings, 0);
    }
    void acceleratedDotPrecision() {
        std::vector<float> a(520), b(520);
        for (int i = 0; i < 520; ++i) {
            a[i] = std::sin(float(i) * .43f);
            b[i] = std::cos(float(i) * .71f);
        }
        for (int length : {0, 1, 3, 7, 31, 32, 127, 512, 513}) {
            double reference = 0;
            for (int i = 0; i < length; ++i)
                reference += double(a[i + 1]) * b[i + 1];
            QVERIFY(std::abs(dot(a.data() + 1, b.data() + 1, length) - reference) < 2e-5);
        }
    }
    void appearanceVariantsAndTransparency() {
        QImage base(100, 100, QImage::Format_ARGB32);
        base.fill(qRgba(15, 200, 66, 0));
        {
            QPainter p(&base);
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(220, 40, 40));
            p.drawEllipse(30, 10, 40, 80);
        }
        const auto v = appearanceDescriptor(base);
        QImage padded(150, 150, QImage::Format_ARGB32);
        padded.fill(qRgba(200, 55, 15, 0));
        {
            QPainter p(&padded);
            p.drawImage(25, 25, base);
        }
        const auto same = appearanceDescriptor(padded);
        QVERIFY(dot(v.data(), same.data()) > .9999f);
        // Invisible colors/padding must not alter the semantic model input either.
        QCOMPARE(Inference::preprocess(base), Inference::preprocess(padded));
        const auto resized = appearanceDescriptor(base.scaled(200, 200));
        QVERIFY(dot(v.data(), resized.data()) > .98f);
        QImage blue = base;
        for (int y = 0; y < blue.height(); ++y)
            for (int x = 0; x < blue.width(); ++x)
                if (qAlpha(blue.pixel(x, y)))
                    blue.setPixel(x, y, qRgba(40, 40, 220, 255));
        const auto other = appearanceDescriptor(blue);
        QVERIFY(dot(v.data(), resized.data()) > dot(v.data(), other.data()) + .15f);
        QImage blank(20, 20, QImage::Format_ARGB32);
        blank.fill(Qt::transparent);
        const auto empty = appearanceDescriptor(blank);
        QVERIFY(std::isfinite(dot(empty.data(), empty.data())));
        QVERIFY_EXCEPTION_THROWN(appearanceDescriptor(QImage()), std::runtime_error);
    }
    void appearanceUpgradeAndSearchWithoutModels() {
        QTemporaryDir directory;
        Paths paths{directory.path() + QStringLiteral("/data")};
        paths.create();
        const auto folder = directory.path() + QStringLiteral("/assets");
        QVERIFY(QDir().mkpath(folder));
        Database database(paths);
        database.initialize();
        database.addFolder(folder);
        std::vector<float> embedding(Dimensions, 0.f);
        embedding[0] = 1;
        for (int i = 0; i < 3; ++i) {
            QImage image(96, 96, QImage::Format_ARGB32);
            image.fill(Qt::transparent);
            {
                QPainter painter(&image);
                painter.fillRect(20, 10, 56, 76, i == 2 ? Qt::blue : Qt::red);
            }
            const auto path = folder + QStringLiteral("/%1.png").arg(i);
            QVERIFY(image.save(path));
            Photo photo;
            photo.path = path;
            photo.folder = folder;
            photo.name = QFileInfo(path).fileName();
            photo.modified = QFileInfo(path).lastModified().toMSecsSinceEpoch();
            photo.bytes = QFileInfo(path).size();
            database.save(photo, embedding);
        }
        QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET model=?"),
                            {CompatibleLegacyModelVersion}));
        const auto source = database.photos().last();
        database.setFavorite(source.id, true);
        database.setNotes(source.id, QStringLiteral("Keep my asset note"));
        const auto before = database.catalog();
        QCOMPARE(database.stats().appearances, 0);
        Indexer indexer(paths);
        int changed = -1, skipped = -1, failed = -1;
        connect(&indexer, &Indexer::completed, this,
                [&](int c, int s, int f, bool, const QStringList &) {
                    changed = c;
                    skipped = s;
                    failed = f;
                });
        // This path must never require a model directory or touch legacy embeddings.
        indexer.scan({folder}, false, false, true);
        QCOMPARE(changed, 3);
        QCOMPARE(failed, 0);
        QCOMPARE(database.stats().appearances, 3);
        QCOMPARE(database.catalog().vectors, before.vectors);
        QCOMPARE(database.stats().outdated, 3);
        QVERIFY(database.photo(source.id).favorite);
        QCOMPARE(database.photo(source.id).notes, QStringLiteral("Keep my asset note"));
        indexer.scan({folder}, false, false, true);
        QCOMPARE(changed, 0);
        QCOMPARE(skipped, 3);
        Searcher searcher(paths);
        SearchResult result;
        connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
        SearchRequest request;
        request.similarId = source.id;
        request.similarity = SimilarityMode::Appearance;
        searcher.search(request);
        QCOMPARE(result.photos.size(), 2);
        QCOMPARE(result.photos.first().name, QStringLiteral("1.png"));
        QVERIFY(result.photos.first().score > result.photos.last().score + .15f);
        QVERIFY(result.status.startsWith(QStringLiteral("Appearance")));
        // Switching catalogs must restore subject vectors and normal browsing.
        request.similarId = 0;
        searcher.search(request);
        QCOMPARE(result.photos.size(), 3);
        QVERIFY(editFixture(paths,
                            QStringLiteral("UPDATE photos SET modified=modified+1 WHERE id=?"),
                            {source.id}));
        QCOMPARE(database.stats().appearances, 2);
        searcher.reload();
        request.similarId = source.id;
        searcher.search(request);
        QVERIFY(result.photos.isEmpty());
        QVERIFY(result.status.contains(QStringLiteral("Build the appearance index")));
        database.removeFolder(folder);
        QCOMPARE(database.stats().appearances, 0);
    }
    void preciseTextMatching() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        std::vector<float> embedding(Dimensions, 0.f);
        embedding[0] = 1;
        for (const auto &name :
             {QStringLiteral("red_sword_01.png"), QStringLiteral("red_shield.png"),
              QStringLiteral("blue_sword.png"), QStringLiteral("sword_red.png")}) {
            Photo photo;
            photo.folder = directory.path();
            photo.path = directory.path() + u'/' + name;
            photo.name = name;
            database.save(photo, embedding);
        }
        SearchRequest request;
        request.mode = SearchMode::Text;
        QCOMPARE(database.lexical(QStringLiteral("red sword"), request).size(), 2);
        QCOMPARE(database.lexical(QStringLiteral("\"red sword\""), request).size(), 1);
        QCOMPARE(database.lexical(QStringLiteral("swo*"), request).size(), 3);
        QCOMPARE(database.lexical(QStringLiteral("red helmet"), request).size(), 0);
        QVERIFY(database.lexical(QStringLiteral("\" OR * ; DROP TABLE photos"), request).isEmpty());
        QCOMPARE(database.count(), 4);
    }
    void textModesSortingAndPagination() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        const auto folder = normalizedPath(directory.path());
        database.addFolder(folder);
        std::vector<float> vector(Dimensions, 0.f);
        vector[0] = 1;
        database.begin();
        for (int i = 0; i < 725; ++i) {
            Photo photo;
            photo.folder = folder;
            photo.path = folder + QStringLiteral("/%1.%2")
                                      .arg(i, 4, 10, QLatin1Char('0'))
                                      .arg(i % 2 ? QStringLiteral("jpg") : QStringLiteral("png"));
            photo.name = QFileInfo(photo.path).fileName();
            photo.tags = QStringLiteral("invoice");
            photo.modified = i;
            photo.width = 400 + i;
            photo.height = 300;
            database.save(photo, vector);
        }
        database.commit();
        const auto last = database.photos().first();
        database.setFavorite(last.id, true);
        Searcher searcher(paths);
        SearchResult result;
        connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
        SearchRequest request;
        request.mode = SearchMode::Text;
        request.query = QStringLiteral("invoice");
        request.sort = SortOrder::Oldest;
        request.limit = 25;
        searcher.search(request);
        QCOMPARE(result.total, 725);
        QCOMPARE(result.photos.size(), 25);
        QCOMPARE(result.photos.first().modified, qint64(0));
        QVERIFY(result.status.startsWith(QStringLiteral("Text matches")));
        request.limit = 650;
        searcher.search(request);
        QCOMPARE(result.photos.size(), 650);
        QCOMPARE(result.photos.last().modified, qint64(649));
        request.favorites = true;
        searcher.search(request);
        QCOMPARE(result.total, 1);
        QCOMPARE(result.photos.first().id, last.id);
        request.favorites = false;
        request.extension = QStringLiteral("jpg");
        request.sort = SortOrder::Largest;
        searcher.search(request);
        QCOMPARE(result.total, 362);
        QCOMPARE(result.photos.first().modified, qint64(723));
        request.extension.clear();
        request.query = QStringLiteral("no_such_invoice_token");
        searcher.search(request);
        QCOMPARE(result.total, 0);
        request.query = QStringLiteral("absentuniquetoken");
        searcher.search(request);
        QCOMPARE(result.total, 0);
        request.query.clear();
        request.sort = SortOrder::Name;
        request.limit = 10;
        searcher.search(request);
        QCOMPARE(result.photos.first().name, QStringLiteral("0000.png"));
        request.limit = -5;
        searcher.search(request);
        QVERIFY(result.photos.isEmpty());
        QSignalSpy spy(&searcher, &Searcher::results);
        searcher.setLatest(10);
        request.generation = 9;
        searcher.search(request);
        QCOMPARE(spy.size(), 0);
    }
    void scopedTextSearchAndMetadataRefresh() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        const auto folder = normalizedPath(directory.path());
        const auto second = folder + QStringLiteral("/second");
        database.addFolder(folder);
        database.addFolder(second);
        std::vector<float> vector(Dimensions, 0.f);
        vector[0] = 1;
        Photo photo;
        photo.folder = folder;
        photo.path = folder + QStringLiteral("/one.png");
        photo.name = QStringLiteral("one.png");
        photo.ocr = QStringLiteral("receipt");
        database.save(photo, vector);
        photo.folder = second;
        photo.path = second + QStringLiteral("/two.png");
        database.save(photo, vector);
        SearchRequest request;
        request.mode = SearchMode::Text;
        request.folder = folder;
        QCOMPARE(database.lexical(QStringLiteral("receipt"), request).size(), 1);
        auto first = database.photos().last();
        Searcher searcher(paths);
        searcher.reload();
        database.setFavorite(first.id, true);
        database.setNotes(first.id, QStringLiteral("travel reimbursement"));
        searcher.updateMetadata(database.photo(first.id));
        request.favorites = true;
        request.query = QStringLiteral("reimbursement");
        SearchResult result;
        connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
        searcher.search(request);
        QCOMPARE(result.photos.size(), 1);
        QCOMPARE(result.photos.first().id, first.id);
    }
    void textInferenceDoesNotLoadVision() {
        const auto models = qEnvironmentVariable("PICLOCATE_MODEL_DIR");
        if (models.isEmpty())
            QSKIP("Set PICLOCATE_MODEL_DIR for model integration");
        QTemporaryDir directory;
        for (const auto &file :
             {QStringLiteral("text_model_quantized.onnx"), QStringLiteral("tokenizer.json")})
            QVERIFY(QFile::copy(models + u'/' + file, directory.path() + u'/' + file));
        Inference inference(directory.path());
        inference.enableGpu();
        QVERIFY(!inference.usesGpu());
        QVERIFY(!inference.gpuFailure().isEmpty());
        QCOMPARE(inference.text(QStringLiteral("green")).size(), size_t(Dimensions));
        QImage image(100, 100, QImage::Format_RGB32);
        image.fill(Qt::green);
        QVERIFY_EXCEPTION_THROWN(inference.image(image), std::runtime_error);
    }
    void catalogCompatibilityAndInvalidVectors() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        std::vector<float> vector(Dimensions, 0);
        vector[0] = 1;
        QHash<QString, qint64> ids;
        const QStringList names{QStringLiteral("current"),   QStringLiteral("legacy"),
                                QStringLiteral("unrelated"), QStringLiteral("missing-model"),
                                QStringLiteral("short"),     QStringLiteral("null"),
                                QStringLiteral("nan"),       QStringLiteral("infinity"),
                                QStringLiteral("zero")};
        for (const auto &name : names) {
            Photo photo;
            photo.folder = normalizedPath(directory.path());
            photo.path = directory.path() + u'/' + name + QStringLiteral(".png");
            photo.name = name;
            photo.tags = QStringLiteral("recoverable");
            database.save(photo, vector);
        }
        for (const auto &photo : database.photos())
            ids.insert(photo.name, photo.id);
        QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET model=? WHERE name='legacy'"),
                            {CompatibleLegacyModelVersion}));
        QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET model='unrelated-512d' "
                                                  "WHERE name='unrelated'")));
        QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET model=NULL "
                                                  "WHERE name='missing-model'")));
        QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET embedding=x'0000' "
                                                  "WHERE name='short'")));
        QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET embedding=NULL "
                                                  "WHERE name='null'")));
        for (const auto &name :
             {QStringLiteral("nan"), QStringLiteral("infinity"), QStringLiteral("zero")}) {
            vector[0] = name == QStringLiteral("nan") ? std::numeric_limits<float>::quiet_NaN()
                        : name == QStringLiteral("infinity")
                            ? std::numeric_limits<float>::infinity()
                            : 0.f;
            const QByteArray bytes(reinterpret_cast<const char *>(vector.data()),
                                   Dimensions * int(sizeof(float)));
            QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET embedding=? WHERE name=?"),
                                {bytes, name}));
        }
        const auto catalog = database.catalog();
        QCOMPARE(catalog.photos.size(), names.size());
        QCOMPARE(catalog.vectors.size(), size_t(names.size()) * Dimensions);
        QCOMPARE(catalog.semanticAvailable.size(), size_t(names.size()));
        QCOMPARE(catalog.legacyEmbeddings, 1);
        QCOMPARE(catalog.unavailableEmbeddings, 7);
        for (qsizetype i = 0; i < catalog.photos.size(); ++i) {
            const bool valid = catalog.photos[i].name == QStringLiteral("current") ||
                               catalog.photos[i].name == QStringLiteral("legacy");
            QCOMPARE(bool(catalog.semanticAvailable[size_t(i)]), valid);
            QCOMPARE(catalog.vectors[size_t(i) * Dimensions], valid ? 1.f : 0.f);
        }
        Searcher searcher(paths);
        SearchResult result;
        connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
        SearchRequest request;
        searcher.search(request);
        QCOMPARE(result.photos.size(), names.size());
        request.similarId = ids[QStringLiteral("legacy")];
        searcher.search(request);
        QCOMPARE(result.photos.size(), 1);
        QCOMPARE(result.photos.first().id, ids[QStringLiteral("current")]);
        QCOMPARE(result.photos.first().score, 1.f);
        request.similarId = ids[QStringLiteral("current")];
        searcher.search(request);
        QCOMPARE(result.photos.size(), 1);
        QCOMPARE(result.photos.first().id, ids[QStringLiteral("legacy")]);
        request.similarId = ids[QStringLiteral("unrelated")];
        searcher.search(request);
        QVERIFY(result.photos.isEmpty());
        QVERIFY(result.status.contains(QStringLiteral("Rescan")));
        request.similarId = 0;
        request.query = QStringLiteral("recoverable");
        searcher.search(request);
        QCOMPARE(result.photos.size(), names.size());
        for (const auto &photo : result.photos)
            QVERIFY(std::isfinite(photo.score));
    }
    void legacyLibrarySemanticSearch() {
        if (qEnvironmentVariable("PICLOCATE_MODEL_DIR").isEmpty())
            QSKIP("Set PICLOCATE_MODEL_DIR for legacy semantic search regression");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        Inference inference(paths.models());
        auto vector = inference.text(QStringLiteral("green"));
        Photo photo;
        photo.folder = normalizedPath(directory.path());
        photo.path = directory.path() + QStringLiteral("/one.png");
        photo.name = QStringLiteral("one.png");
        photo.width = 800;
        photo.height = 400;
        database.save(photo, vector);
        const auto legacyId = database.photos().first().id;
        database.setFavorite(legacyId, true);
        database.setNotes(legacyId, QStringLiteral("Keep my description"));
        for (auto &value : vector)
            value = -value;
        photo.path = directory.path() + QStringLiteral("/two.png");
        photo.name = QStringLiteral("two.png");
        database.save(photo, vector);
        QVERIFY(editFixture(paths, QStringLiteral("UPDATE photos SET model=?"),
                            {CompatibleLegacyModelVersion}));
        QCOMPARE(database.stats().outdated, 2);
        for (const auto &entry : database.scanEntries())
            QVERIFY(!entry.embeddingCurrent);
        QVERIFY(database.lexical(QStringLiteral("green")).isEmpty());
        Searcher searcher(paths);
        SearchResult result;
        connect(&searcher, &Searcher::results, this, [&](const auto &r) { result = r; });
        SearchRequest request;
        request.query = QStringLiteral("green");
        searcher.search(request);
        QCOMPARE(result.photos.size(), 2);
        QCOMPARE(result.photos.first().id, legacyId);
        QVERIFY(result.photos.first().score > .99f);
        QVERIFY(result.status.contains(QStringLiteral("Earlier index supported")));
        request.favorites = true;
        request.folder = photo.folder;
        request.orientation = 1;
        searcher.search(request);
        QCOMPARE(result.photos.size(), 1);
        QCOMPARE(result.photos.first().id, legacyId);
        // Reading/searching never silently relabels or discards the existing index.
        QCOMPARE(database.stats().outdated, 2);
        auto legacy = database.photo(legacyId);
        QVERIFY(legacy.favorite);
        QCOMPARE(legacy.notes, QStringLiteral("Keep my description"));
        for (auto &value : vector)
            value = -value;
        database.save(legacy, vector);
        QCOMPARE(database.stats().outdated, 1);
        QVERIFY(database.scanEntries().value(legacy.path).embeddingCurrent);
        QVERIFY(!database.scanEntries().value(photo.path).embeddingCurrent);
        QCOMPARE(database.photo(legacyId).notes, legacy.notes);
        QVERIFY(database.photo(legacyId).favorite);
        searcher.reload();
        searcher.search(request);
        QCOMPARE(result.photos.first().id, legacyId);
    }
    void normalizedVectors() {
        std::vector<float> v{3, 4};
        normalize(v);
        QVERIFY(std::abs(v[0] - .6f) < 1e-6);
        QVERIFY(std::abs(dot(v.data(), v.data(), 2) - 1) < 1e-6);
        std::vector<float> bad{0, 0};
        QVERIFY_EXCEPTION_THROWN(normalize(bad), std::runtime_error);
    }
    void exactTopK() {
        std::vector<float> v(Dimensions * 3, 0), q(Dimensions, 0);
        q[0] = 1;
        v[0] = .2f;
        v[Dimensions] = .9f;
        v[Dimensions * 2] = -.1f;
        auto r = topK(v, q, 2);
        QCOMPARE(r.size(), 2);
        QCOMPARE(r[0].first, 1);
        QCOMPARE(r[1].first, 0);
        QVERIFY(topK(v, {}, 1).isEmpty());
        QCOMPARE(topK(v, q, 99).size(), 3);
    }
    void databasePersistence() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Paths paths{dir.path()};
        paths.create();
        qint64 id;
        {
            Database db(paths);
            db.initialize();
            db.addFolder(dir.path());
            Photo p;
            p.path = dir.path() + QStringLiteral("/test.png");
            p.folder = normalizedPath(dir.path());
            p.name = QStringLiteral("test.png");
            p.width = 640;
            p.height = 480;
            p.modified = 12;
            p.bytes = 200;
            p.ocr = QStringLiteral("invoice 2025");
            std::vector<float> v(Dimensions, 0);
            v[0] = 1;
            db.begin();
            db.save(p, v);
            db.commit();
            QCOMPARE(db.count(), 1);
            id = db.photos()[0].id;
            db.setFavorite(id, true);
            db.setNotes(id, QStringLiteral("Private note"));
            p.modified = 13;
            db.save(p, v);
            QVERIFY(db.photo(id).favorite);
            QCOMPARE(db.photo(id).notes, QStringLiteral("Private note"));
            auto catalog = db.catalog();
            QCOMPARE(catalog.photos.size(), 1);
            QCOMPARE(catalog.vectors.size(), size_t(Dimensions));
            QCOMPARE(catalog.vectors[0], 1.f);
        }
        {
            Database db(paths);
            db.initialize();
            QVERIFY(db.photo(id).favorite);
            QCOMPARE(db.photo(id).modified, qint64(13));
            db.prune(normalizedPath(dir.path()), {});
            QCOMPARE(db.count(), 0);
        }
    }
    void folderRemovalDoesNotDeleteFiles() {
        QTemporaryDir dir;
        Paths paths{dir.path()};
        paths.create();
        Database db(paths);
        db.initialize();
        db.addFolder(dir.path());
        Photo p;
        p.path = dir.path() + QStringLiteral("/original.png");
        p.folder = normalizedPath(dir.path());
        QFile original(p.path);
        QVERIFY(original.open(QIODevice::WriteOnly));
        original.write("original");
        original.close();
        std::vector<float> v(Dimensions, 1);
        db.save(p, v);
        db.removeFolder(p.folder);
        QCOMPARE(db.count(), 0);
        QVERIFY(QFileInfo::exists(p.path));
    }
    void imagePreprocessing() {
        QImage red(500, 300, QImage::Format_RGB32);
        red.fill(Qt::red);
        auto values = Inference::preprocess(red);
        QCOMPARE(values.size(), size_t(3 * 224 * 224));
        QVERIFY(std::abs(values[0] - (1 - .48145466f) / .26862954f) < 1e-5);
        QVERIFY(std::abs(values[224 * 224] + .4578275f / .26130258f) < 1e-5);
    }
    void tokenizer() {
        const auto dir = qEnvironmentVariable("PICLOCATE_MODEL_DIR");
        if (dir.isEmpty())
            QSKIP("Set PICLOCATE_MODEL_DIR for tokenizer parity test");
        Tokenizer t;
        t.load(dir + QStringLiteral("/tokenizer.json"));
        auto ids = t.encode(QStringLiteral("a photo of a cat"));
        QCOMPARE(ids.size(), size_t(77));
        const std::vector<int64_t> expected{49406, 320, 1125, 539, 320, 2368, 49407};
        for (size_t i = 0; i < expected.size(); ++i)
            QCOMPARE(ids[i], expected[i]);
        QCOMPARE(t.encode(QStringLiteral("  A PHOTO\n of a CAT  ")), ids);
        auto longText = t.encode(QStringLiteral("word ").repeated(100));
        QCOMPARE(longText.back(), int64_t(49407));
    }
    void tokenizerReferenceCases() {
        const auto dir = qEnvironmentVariable("PICLOCATE_MODEL_DIR");
        if (dir.isEmpty())
            QSKIP("Set PICLOCATE_MODEL_DIR for tokenizer reference tests");
        QFile file(QFINDTESTDATA("tokenizer_cases.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        Tokenizer tokenizer;
        tokenizer.load(dir + QStringLiteral("/tokenizer.json"));
        for (const auto &entry : QJsonDocument::fromJson(file.readAll()).array()) {
            auto c = entry.toObject();
            auto actual = tokenizer.encode(c.value(QStringLiteral("text")).toString());
            auto expected = c.value(QStringLiteral("ids")).toArray();
            QCOMPARE(actual.size(), size_t(expected.size()));
            for (size_t i = 0; i < actual.size(); ++i)
                QCOMPARE(actual[i], qint64(expected[int(i)].toInt()));
        }
    }
    void lexicalUpdates() {
        QTemporaryDir dir;
        Paths paths{dir.path()};
        paths.create();
        Database db(paths);
        db.initialize();
        db.addFolder(dir.path());
        Photo p;
        p.path = dir.path() + QStringLiteral("/photo.png");
        p.folder = normalizedPath(dir.path());
        p.ocr = QStringLiteral("INVOICE 4172");
        std::vector<float> v(Dimensions, 1);
        db.save(p, v);
        auto id = db.photos()[0].id;
        QVERIFY(db.lexical(QStringLiteral("invoice 4172")).contains(id));
        db.setNotes(id, QStringLiteral("Café résumé"));
        QVERIFY(db.lexical(QStringLiteral("cafe")).contains(id));
        db.setNotes(id, QStringLiteral("replaced"));
        QVERIFY(db.lexical(QStringLiteral("cafe")).isEmpty());
        QVERIFY(db.lexical(QStringLiteral("\" OR * ; DROP TABLE photos")).isEmpty());
        db.removeFolder(p.folder);
        QVERIFY(db.lexical(QStringLiteral("invoice")).isEmpty());
    }
    void searchFilters() {
        QTemporaryDir dir;
        Paths paths{dir.path()};
        paths.create();
        Database db(paths);
        db.initialize();
        db.addFolder(dir.path());
        Photo p;
        p.folder = normalizedPath(dir.path());
        p.path = dir.path() + QStringLiteral("/a.png");
        p.width = 800;
        p.height = 400;
        std::vector<float> v(Dimensions, 0);
        v[0] = 1;
        db.save(p, v);
        auto id = db.photos()[0].id;
        db.setFavorite(id, true);
        p.path = dir.path() + QStringLiteral("/b.png");
        p.width = 300;
        p.height = 800;
        db.save(p, v);
        Searcher s(paths);
        SearchResult result;
        connect(&s, &Searcher::results, this, [&](const auto &r) { result = r; });
        SearchRequest r;
        r.favorites = true;
        s.search(r);
        QCOMPARE(result.photos.size(), 1);
        QCOMPARE(result.photos[0].id, id);
        r.favorites = false;
        r.orientation = 2;
        s.search(r);
        QCOMPARE(result.photos.size(), 1);
        QVERIFY(result.photos[0].id != id);
        r.orientation = 0;
        r.similarId = id;
        s.search(r);
        QCOMPARE(result.photos.size(), 1);
        QVERIFY(result.photos[0].id != id);
    }
    void optionalModelDownload() {
        const auto path = qEnvironmentVariable("PICLOCATE_DOWNLOAD_TEST_DIR");
        if (path.isEmpty())
            QSKIP("Set PICLOCATE_DOWNLOAD_TEST_DIR for network model installation test");
        ModelDownload download;
        QSignalSpy spy(&download, &ModelDownload::finished);
        download.start(path);
        QTRY_COMPARE_WITH_TIMEOUT(spy.size(), 1, 60000);
        QVERIFY2(spy[0][0].toBool(), qPrintable(spy[0][1].toString()));
        QVERIFY(modelsPresent(path));
    }
    void incrementalIndexingAndRecovery() {
        if (qEnvironmentVariable("PICLOCATE_MODEL_DIR").isEmpty())
            QSKIP("Set PICLOCATE_MODEL_DIR for native indexing integration");
        QTemporaryDir directory;
        Paths paths{directory.path() + QStringLiteral("/data")};
        paths.create();
        const auto folder = directory.path() + QStringLiteral("/photos");
        QVERIFY(QDir().mkpath(folder));
        for (int i = 0; i < 3; ++i) {
            QImage image(320, 240, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 90, 200, 180));
            QVERIFY(image.save(folder + QStringLiteral("/%1.png").arg(i)));
        }
        Database database(paths);
        database.initialize();
        database.addFolder(folder);
        Indexer indexer(paths);
        bool paused = false;
        int changed = 0, skipped = 0, failed = 0;
        connect(&indexer, &Indexer::completed, this,
                [&](int c, int s, int f, bool cancelled, const QStringList &) {
                    changed = c;
                    skipped = s;
                    failed = f;
                    paused = cancelled;
                });
        auto cancellation = connect(&indexer, &Indexer::progress, this,
                                    [&](int done, int, const QString &, int, int) {
                                        if (done > 0)
                                            indexer.cancel();
                                    });
        indexer.scan({folder}, false, false);
        QVERIFY(paused);
        QVERIFY(database.count() >= 1);
        disconnect(cancellation);
        indexer.prepare();
        indexer.scan({folder}, false, false);
        QVERIFY(!paused);
        QCOMPARE(database.count(), 3);
        QCOMPARE(failed, 0);
        indexer.scan({folder}, false, false);
        QCOMPARE(changed, 0);
        QCOMPARE(skipped, 3);
        auto photo = database.photos()[0];
        database.setFavorite(photo.id, true);
        database.setNotes(photo.id, QStringLiteral("Keep this note"));
        QImage updated(410, 300, QImage::Format_RGB32);
        updated.fill(Qt::yellow);
        QVERIFY(updated.save(photo.path));
        QFile broken(folder + QStringLiteral("/broken.png"));
        QVERIFY(broken.open(QIODevice::WriteOnly));
        broken.write("not an image");
        broken.close();
        indexer.scan({folder}, false, false);
        QCOMPARE(changed, 1);
        QCOMPARE(failed, 1);
        QVERIFY(database.photo(photo.id).favorite);
        QCOMPARE(database.photo(photo.id).notes, QStringLiteral("Keep this note"));
        QVERIFY(QDir().rename(folder, folder + QStringLiteral("-offline")));
        indexer.scan({folder}, false, false);
        QCOMPARE(database.count(), 3);
    }
    void preprocessingReferenceExport() {
        const auto dir = qEnvironmentVariable("PICLOCATE_PARITY_DIR");
        if (dir.isEmpty())
            QSKIP("Optional reference export");
        QDirIterator it(dir, {QStringLiteral("*.png"), QStringLiteral("*.jpg")}, QDir::Files);
        while (it.hasNext()) {
            const auto path = it.next();
            auto values = Inference::preprocess(readImage(path));
            QFile output(path + QStringLiteral(".f32"));
            QVERIFY(output.open(QIODevice::WriteOnly));
            output.write(reinterpret_cast<const char *>(values.data()),
                         qint64(values.size() * sizeof(float)));
        }
    }
    void preprocessingNativeSizeNormalization() {
        // Exercise all 256 byte values in every channel, including externally
        // padded scanlines. Expected values follow CLIP's published transform.
        constexpr int stride = 224 * 3 + 16;
        QByteArray storage(stride * 224, char(0x7f));
        QImage image(reinterpret_cast<uchar *>(storage.data()), 224, 224, stride,
                     QImage::Format_RGB888);
        std::vector<float> expected(3 * 224 * 224);
        constexpr float mean[] = {.48145466f, .4578275f, .40821073f},
                        deviation[] = {.26862954f, .26130258f, .27577711f};
        for (int y = 0; y < 224; ++y)
            for (int x = 0; x < 224; ++x)
                for (int c = 0; c < 3; ++c) {
                    const auto value = uchar((x + y * 37 + c * 71) % 256);
                    image.scanLine(y)[x * 3 + c] = value;
                    const float unit = float(value) / 255;
                    expected[size_t(c) * 224 * 224 + y * 224 + x] = (unit - mean[c]) / deviation[c];
                }
        for (auto format : {QImage::Format_RGB888, QImage::Format_RGB32, QImage::Format_RGBA8888,
                            QImage::Format_ARGB32_Premultiplied}) {
            const auto actual = Inference::preprocess(image.convertToFormat(format));
            QCOMPARE(actual, expected);
        }
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
