#include "library/database.h"
#include "core/core.h"
#include "models/appearance.h"
#include <cmath>
namespace piclocate {
static void checked(QSqlQuery &q) {
    if (!q.exec())
        throw std::runtime_error(q.lastError().text().toStdString());
}
static Photo row(const QSqlQuery &q) {
    Photo p;
    p.id = q.value(0).toLongLong();
    p.path = q.value(1).toString();
    p.folder = q.value(2).toString();
    p.name = q.value(3).toString();
    p.thumb = q.value(4).toString();
    p.width = q.value(5).toInt();
    p.height = q.value(6).toInt();
    p.bytes = q.value(7).toLongLong();
    p.modified = q.value(8).toLongLong();
    p.favorite = q.value(9).toBool();
    p.tags = q.value(10).toString();
    p.ocr = q.value(11).toString();
    p.notes = q.value(12).toString();
    return p;
}
static const QString Columns =
    QStringLiteral("id,path,folder,name,thumb,width,height,bytes,modified,favorite,tags,ocr,notes");
Database::Database(const Paths &p) : connection_(QUuid::createUuid().toString()) {
    db_ = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection_);
    db_.setDatabaseName(p.database());
    db_.setConnectOptions(QStringLiteral("QSQLITE_BUSY_TIMEOUT=10000"));
    try {
        if (!db_.open())
            throw std::runtime_error(db_.lastError().text().toStdString());
        exec(QStringLiteral("PRAGMA foreign_keys=ON"));
        // synchronous is connection-local; index/metadata workers need this too.
        exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    } catch (...) {
        // A throwing constructor has no destructor to remove its Qt connection.
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection_);
        throw;
    }
}
Database::~Database() {
    saveQuery_.reset();
    appearanceQuery_.reset();
    thumbnailQuery_.reset();
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection_);
}
QSqlQuery &Database::prepared(std::unique_ptr<QSqlQuery> &query, const QString &sql) {
    if (!query) {
        auto candidate = std::make_unique<QSqlQuery>(db_);
        if (!candidate->prepare(sql))
            throw std::runtime_error(candidate->lastError().text().toStdString());
        query = std::move(candidate);
    }
    return *query;
}
static QString searchableName(const QString &name) {
    // Keep the original identifier searchable, and expose its readable words.
    // XMLSword, FireSwordIcon and Sword02 all have meaningful boundaries.
    static const QRegularExpression boundaries(
        QStringLiteral("(?<=[\\p{Ll}\\p{N}])(?=\\p{Lu})|"
                       "(?<=\\p{Lu})(?=\\p{Lu}\\p{Ll})|"
                       "(?<=\\p{L})(?=\\p{N})|(?<=\\p{N})(?=\\p{L})"),
        QRegularExpression::UseUnicodePropertiesOption);
    const auto normalized = name.normalized(QString::NormalizationForm_C);
    auto readable = normalized;
    readable.replace(boundaries, QStringLiteral(" "));
    // Ordinary filenames already have the same tokens in the original column.
    // Avoid doubling postings, which slows broad matches across large libraries.
    return readable == normalized ? QStringLiteral("") : readable;
}
void Database::exec(const QString &sql) const {
    QSqlQuery q(db_);
    if (!q.exec(sql))
        throw std::runtime_error(q.lastError().text().toStdString());
}
void Database::initialize() {
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    QSqlQuery versionQuery(db_);
    if (!versionQuery.exec(QStringLiteral("PRAGMA user_version")) || !versionQuery.next())
        throw std::runtime_error("Could not read the library schema version");
    const int version = versionQuery.value(0).toInt();
    versionQuery.finish();
    if (version > 4)
        throw std::runtime_error("This library needs a newer version of PicLocate");
    begin();
    try {
        exec(QStringLiteral("CREATE TABLE IF NOT EXISTS folders(path TEXT PRIMARY KEY)"));
        exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS photos(id INTEGER PRIMARY KEY,path TEXT UNIQUE NOT "
            "NULL,folder "
            "TEXT NOT NULL REFERENCES folders(path) ON DELETE CASCADE,name TEXT,thumb TEXT,width "
            "INTEGER,height INTEGER,bytes INTEGER,modified INTEGER,favorite INTEGER DEFAULT 0,tags "
            "TEXT DEFAULT '',ocr TEXT DEFAULT '',notes TEXT DEFAULT '',embedding BLOB,model "
            "TEXT)"));
        exec(QStringLiteral("CREATE INDEX IF NOT EXISTS photos_folder ON photos(folder)"));
        QSqlQuery columns(db_);
        if (!columns.exec(QStringLiteral("PRAGMA table_info(photos)")))
            throw std::runtime_error(columns.lastError().text().toStdString());
        bool hasSearchName = false;
        while (columns.next())
            hasSearchName |= columns.value(1).toString() == QStringLiteral("search_name");
        columns.finish();
        const bool migrateNames = !hasSearchName || version < 4;
        QSqlQuery exists(db_);
        if (!exists.exec(QStringLiteral("SELECT name FROM sqlite_master WHERE name='photo_text'")))
            throw std::runtime_error(exists.lastError().text().toStdString());
        const bool rebuild = migrateNames || !exists.next();
        exists.finish();
        if (rebuild) {
            exec(QStringLiteral("DROP TRIGGER IF EXISTS photos_ai"));
            exec(QStringLiteral("DROP TRIGGER IF EXISTS photos_ad"));
            exec(QStringLiteral("DROP TRIGGER IF EXISTS photos_au"));
            exec(QStringLiteral("DROP TABLE IF EXISTS photo_text"));
        }
        if (!hasSearchName)
            exec(QStringLiteral(
                "ALTER TABLE photos ADD COLUMN search_name TEXT NOT NULL DEFAULT ''"));
        if (migrateNames) {
            // Change only the text index, transactionally. Preserve every photo ID,
            // vector, preview, favorite and description; no image scan is required.
            QSqlQuery names(db_);
            names.setForwardOnly(true);
            if (!names.exec(QStringLiteral("SELECT id,name FROM photos")))
                throw std::runtime_error(names.lastError().text().toStdString());
            QList<QPair<qint64, QString>> updates;
            while (names.next())
                updates.append(
                    {names.value(0).toLongLong(), searchableName(names.value(1).toString())});
            names.finish();
            QSqlQuery update(db_);
            if (!update.prepare(QStringLiteral("UPDATE photos SET search_name=? WHERE id=?")))
                throw std::runtime_error(update.lastError().text().toStdString());
            for (const auto &entry : updates) {
                update.bindValue(0, entry.second);
                update.bindValue(1, entry.first);
                checked(update);
                update.finish();
            }
        }
        exec(QStringLiteral(
            "CREATE VIRTUAL TABLE IF NOT EXISTS photo_text USING "
            "fts5(name,search_name,tags,ocr,notes,content='photos',content_rowid='id',"
            "tokenize='unicode61 remove_diacritics 2')"));
        exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS photos_ai AFTER INSERT ON photos BEGIN "
            "INSERT INTO photo_text(rowid,name,search_name,tags,ocr,notes) "
            "VALUES(new.id,new.name,new.search_name,new.tags,new.ocr,new.notes); END"));
        exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS photos_ad AFTER DELETE ON photos BEGIN "
            "INSERT INTO photo_text(photo_text,rowid,name,search_name,tags,ocr,notes) "
            "VALUES('delete',old.id,old.name,old.search_name,old.tags,old.ocr,old.notes); END"));
        exec(QStringLiteral(
            "CREATE TRIGGER IF NOT EXISTS photos_au AFTER UPDATE OF "
            "name,search_name,tags,ocr,notes "
            "ON photos BEGIN INSERT INTO "
            "photo_text(photo_text,rowid,name,search_name,tags,ocr,notes) "
            "VALUES('delete',old.id,old.name,old.search_name,old.tags,old.ocr,old.notes); INSERT "
            "INTO "
            "photo_text(rowid,name,search_name,tags,ocr,notes) "
            "VALUES(new.id,new.name,new.search_name,new.tags,new.ocr,new.notes); END"));
        if (rebuild)
            exec(QStringLiteral("INSERT INTO photo_text(photo_text) VALUES('rebuild')"));
        exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS appearances("
            "photo_id INTEGER PRIMARY KEY REFERENCES photos(id) ON DELETE CASCADE,"
            "modified INTEGER NOT NULL,bytes INTEGER NOT NULL,version INTEGER NOT NULL,"
            "descriptor BLOB NOT NULL)"));
        exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS thumbnails(key TEXT PRIMARY KEY,data BLOB NOT NULL)"));
        exec(QStringLiteral("CREATE INDEX IF NOT EXISTS photos_thumb ON photos(thumb)"));
        exec(QStringLiteral("PRAGMA user_version=4"));
        commit();
    } catch (...) {
        db_.rollback();
        throw;
    }
}
QString Database::saveThumbnail(const QByteArray &encoded) {
    if (encoded.isEmpty() || encoded.size() > 8 * 1024 * 1024)
        throw std::runtime_error("Invalid thumbnail data");
    const auto key =
        QStringLiteral("piclocate-thumb:") +
        QString::fromLatin1(QCryptographicHash::hash(encoded, QCryptographicHash::Sha256).toHex());
    auto &q = prepared(thumbnailQuery_,
                       QStringLiteral("INSERT OR IGNORE INTO thumbnails(key,data) VALUES(?,?)"));
    q.bindValue(0, key);
    q.bindValue(1, encoded);
    checked(q);
    q.finish();
    return key;
}
QImage Database::thumbnail(const QString &location) const {
    if (!location.startsWith(QStringLiteral("piclocate-thumb:")))
        return readImage(location, 420);
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT data FROM thumbnails WHERE key=?"));
    q.addBindValue(location);
    checked(q);
    return q.next() ? QImage::fromData(q.value(0).toByteArray()) : QImage();
}
void Database::pruneThumbnails() {
    exec(QStringLiteral("DELETE FROM thumbnails WHERE NOT EXISTS(SELECT 1 FROM photos WHERE "
                        "photos.thumb=thumbnails.key)"));
}
QImage readThumbnail(const Paths &paths, const QString &location) {
    if (!location.startsWith(QStringLiteral("piclocate-thumb:")))
        return readImage(location, 420);
    // A thumbnail worker owns its connection. Never share the GUI's Qt SQL handle.
    try {
        Database database(paths);
        return database.thumbnail(location);
    } catch (...) {
        return {};
    }
}
void Database::saveAppearance(const Photo &photo, const std::vector<float> &descriptor) {
    if (descriptor.size() != Dimensions)
        throw std::runtime_error("Invalid appearance descriptor");
    auto &q = prepared(
        appearanceQuery_,
        QStringLiteral(
            "INSERT OR REPLACE INTO appearances(photo_id,modified,bytes,version,descriptor) "
            "SELECT id,modified,bytes,?,? FROM photos WHERE path=? AND modified=? AND bytes=?"));
    q.bindValue(0, AppearanceVersion);
    q.bindValue(1, QByteArray(reinterpret_cast<const char *>(descriptor.data()),
                              Dimensions * sizeof(float)));
    q.bindValue(2, photo.path);
    q.bindValue(3, photo.modified);
    q.bindValue(4, photo.bytes);
    checked(q);
    q.finish();
}
QStringList Database::folders() const {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT path FROM folders ORDER BY path"));
    checked(q);
    QStringList r;
    while (q.next())
        r << q.value(0).toString();
    return r;
}
void Database::addFolder(const QString &path) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("INSERT OR IGNORE INTO folders(path) VALUES(?)"));
    q.addBindValue(normalizedPath(path));
    checked(q);
}
void Database::removeFolder(const QString &path) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("DELETE FROM folders WHERE path=?"));
    q.addBindValue(path);
    checked(q);
    pruneThumbnails();
}
int Database::count() const {
    QSqlQuery q(db_);
    q.exec(QStringLiteral("SELECT count(*) FROM photos"));
    q.next();
    return q.value(0).toInt();
}
Database::Stats Database::stats() const {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT count(*),coalesce(sum(p.bytes),0),"
                             "coalesce(sum(p.model IS NULL OR p.model NOT IN (?,?)),0),"
                             "coalesce(sum(a.version=? AND length(a.descriptor)=? AND "
                             "a.modified=p.modified AND a.bytes=p.bytes),0) "
                             "FROM photos p LEFT JOIN appearances a ON a.photo_id=p.id"));
    q.addBindValue(ModelVersion);
    q.addBindValue(GpuModelVersion);
    q.addBindValue(AppearanceVersion);
    q.addBindValue(Dimensions * int(sizeof(float)));
    checked(q);
    q.next();
    return {q.value(0).toInt(), q.value(2).toInt(), q.value(3).toInt(), q.value(1).toLongLong()};
}
QHash<QString, Database::ScanEntry> Database::scanEntries() const {
    QSqlQuery q(db_);
    q.setForwardOnly(true);
    q.prepare(QStringLiteral(
        "SELECT p.id,p.path,p.modified,p.bytes,p.ocr,"
        "p.model IN (?,?) AND length(p.embedding)=?,"
        "a.version=? AND length(a.descriptor)=? AND a.modified=p.modified AND a.bytes=p.bytes "
        "FROM photos p LEFT JOIN appearances a ON a.photo_id=p.id"));
    q.addBindValue(ModelVersion);
    q.addBindValue(GpuModelVersion);
    q.addBindValue(Dimensions * int(sizeof(float)));
    q.addBindValue(AppearanceVersion);
    q.addBindValue(Dimensions * int(sizeof(float)));
    checked(q);
    QHash<QString, ScanEntry> entries;
    while (q.next()) {
        ScanEntry entry;
        entry.photo.id = q.value(0).toLongLong();
        entry.photo.path = q.value(1).toString();
        entry.photo.modified = q.value(2).toLongLong();
        entry.photo.bytes = q.value(3).toLongLong();
        entry.photo.ocr = q.value(4).toString();
        entry.embeddingCurrent = q.value(5).toBool();
        entry.appearanceCurrent = q.value(6).toBool();
        const auto path = entry.photo.path;
        entries.insert(path, std::move(entry));
    }
    return entries;
}
QHash<qint64, float> Database::lexical(const QString &text, const SearchRequest &scope) const {
    static const QSet<QString> stop{
        QStringLiteral("a"),    QStringLiteral("an"),    QStringLiteral("the"),
        QStringLiteral("of"),   QStringLiteral("in"),    QStringLiteral("on"),
        QStringLiteral("at"),   QStringLiteral("to"),    QStringLiteral("and"),
        QStringLiteral("with"), QStringLiteral("photo"), QStringLiteral("image")};
    static const QRegularExpression word(
        QStringLiteral("\\\"([^\\\"]+)\\\"|([\\p{L}\\p{N}]+)(\\*)?"),
        QRegularExpression::UseUnicodePropertiesOption);
    QStringList terms;
    auto it = word.globalMatch(text.toLower());
    while (it.hasNext() && terms.size() < 32) {
        const auto match = it.next();
        if (!match.captured(1).isEmpty()) {
            static const QRegularExpression punctuation(
                QStringLiteral("[^\\p{L}\\p{N}]+"), QRegularExpression::UseUnicodePropertiesOption);
            auto phrase = match.captured(1);
            phrase.replace(punctuation, QStringLiteral(" "));
            phrase = phrase.simplified();
            if (!phrase.isEmpty())
                terms << u'"' + phrase + u'"';
        } else {
            const auto token = match.captured(2);
            if (scope.mode == SearchMode::Text || !stop.contains(token))
                terms << u'"' + token + u'"' + match.captured(3);
        }
    }
    QHash<qint64, float> result;
    if (terms.isEmpty())
        return result;
    QSqlQuery q(db_);
    QString sql = QStringLiteral("SELECT photo_text.rowid,bm25(photo_text,8.0,8.0,0.25,2.0,6.0) AS "
                                 "relevance FROM photo_text JOIN photos p ON p.id=photo_text.rowid "
                                 "WHERE photo_text MATCH ?");
    if (!scope.folder.isEmpty())
        sql += QStringLiteral(" AND p.folder=?");
    if (scope.favorites)
        sql += QStringLiteral(" AND p.favorite=1");
    if (scope.orientation == 1)
        sql += QStringLiteral(" AND p.width>p.height");
    else if (scope.orientation == 2)
        sql += QStringLiteral(" AND p.width<p.height");
    else if (scope.orientation == 3)
        sql += QStringLiteral(" AND p.width=p.height");
    if (!scope.extension.isEmpty())
        sql += QStringLiteral(" AND lower(p.path) LIKE ?");
    q.prepare(sql);
    // A red sword must match both words. CLIP still supplies broader conceptual
    // matches in Smart mode, without boosting every unrelated red asset.
    q.addBindValue(terms.join(QStringLiteral(" AND ")));
    if (!scope.folder.isEmpty())
        q.addBindValue(scope.folder);
    if (!scope.extension.isEmpty())
        q.addBindValue(QStringLiteral("%.") + scope.extension.toLower());
    checked(q);
    double best = 0;
    while (q.next()) {
        const auto id = q.value(0).toLongLong();
        const double score = -q.value(1).toDouble();
        best = qMax(best, score);
        result.insert(id, float(score));
    }
    if (best > 0)
        for (auto &value : result)
            value = float(.04 + .16 * double(value) / best);
    // User filenames/descriptions are stronger evidence than generated labels.
    // Reuse the parsed FTS expression, preserving AND, phrase and prefix semantics.
    QSqlQuery trusted(db_);
    const bool scoped = !scope.folder.isEmpty() || scope.favorites || scope.orientation ||
                        !scope.extension.isEmpty();
    trusted.prepare(scoped
                        ? QStringLiteral("SELECT photo_text.rowid") +
                              sql.mid(sql.indexOf(QStringLiteral(" FROM ")))
                        : QStringLiteral("SELECT rowid FROM photo_text WHERE photo_text MATCH ?"));
    trusted.addBindValue(QStringLiteral("{name search_name notes}: (") +
                         terms.join(QStringLiteral(" AND ")) + u')');
    if (!scope.folder.isEmpty())
        trusted.addBindValue(scope.folder);
    if (!scope.extension.isEmpty())
        trusted.addBindValue(QStringLiteral("%.") + scope.extension.toLower());
    checked(trusted);
    while (trusted.next()) {
        const auto id = trusted.value(0).toLongLong();
        if (result.contains(id))
            result[id] += .25f;
    }
    return result;
}
Photos Database::photos() const {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT ") + Columns +
              QStringLiteral(" FROM photos ORDER BY modified DESC,id DESC"));
    checked(q);
    Photos r;
    while (q.next())
        r << row(q);
    return r;
}
Photo Database::photo(qint64 id) const {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT ") + Columns + QStringLiteral(" FROM photos WHERE id=?"));
    q.addBindValue(id);
    checked(q);
    return q.next() ? row(q) : Photo{};
}
void Database::save(const Photo &p, const std::vector<float> &v, const QString &model) {
    if (v.size() != Dimensions)
        throw std::runtime_error("Invalid embedding dimensions");
    auto &q = prepared(
        saveQuery_,
        QStringLiteral(
            "INSERT INTO "
            "photos(path,folder,name,thumb,width,height,bytes,modified,tags,ocr,embedding,model,"
            "search_name) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO UPDATE SET "
            "folder=excluded.folder,name=excluded.name,thumb=excluded.thumb,width=excluded.width,"
            "height=excluded.height,bytes=excluded.bytes,modified=excluded.modified,tags=excluded."
            "tags,"
            "ocr=excluded.ocr,embedding=excluded.embedding,model=excluded.model,search_name="
            "excluded.search_name"));
    q.bindValue(0, p.path);
    q.bindValue(1, p.folder);
    q.bindValue(2, p.name);
    q.bindValue(3, p.thumb);
    q.bindValue(4, p.width);
    q.bindValue(5, p.height);
    q.bindValue(6, p.bytes);
    q.bindValue(7, p.modified);
    q.bindValue(8, p.tags.isNull() ? QStringLiteral("") : p.tags);
    q.bindValue(9, p.ocr.isNull() ? QStringLiteral("") : p.ocr);
    q.bindValue(10,
                QByteArray(reinterpret_cast<const char *>(v.data()), Dimensions * sizeof(float)));
    q.bindValue(11, model);
    q.bindValue(12, searchableName(p.name));
    checked(q);
    q.finish();
}
void Database::prune(const QString &folder, const QSet<QString> &seen) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("SELECT id,path FROM photos WHERE folder=?"));
    q.addBindValue(folder);
    checked(q);
    QList<qint64> gone;
    while (q.next())
        if (!seen.contains(q.value(1).toString()))
            gone << q.value(0).toLongLong();
    QSqlQuery d(db_);
    if (!d.prepare(QStringLiteral("DELETE FROM photos WHERE id=?")))
        throw std::runtime_error(d.lastError().text().toStdString());
    for (auto id : gone) {
        d.bindValue(0, id);
        checked(d);
        d.finish();
    }
}
void Database::setFavorite(qint64 id, bool v) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE photos SET favorite=? WHERE id=?"));
    q.addBindValue(v);
    q.addBindValue(id);
    checked(q);
}
void Database::setNotes(qint64 id, const QString &v) {
    QSqlQuery q(db_);
    q.prepare(QStringLiteral("UPDATE photos SET notes=? WHERE id=?"));
    q.addBindValue(v.left(16000));
    q.addBindValue(id);
    checked(q);
}
void Database::begin() {
    if (!db_.transaction())
        throw std::runtime_error(db_.lastError().text().toStdString());
}
void Database::commit() {
    if (!db_.commit())
        throw std::runtime_error(db_.lastError().text().toStdString());
}
Database::Catalog Database::catalog(bool appearance, bool loadVectors) const {
    QSqlQuery q(db_);
    q.setForwardOnly(true);
    const QString metadata =
        QStringLiteral("SELECT "
                       "id,path,folder,name,thumb,width,height,bytes,modified,favorite,tags,"
                       "substr(ocr,1,700),substr(notes,1,700),");
    if (!loadVectors) {
        q.prepare(metadata +
                  QStringLiteral("NULL,NULL FROM photos ORDER BY modified DESC,id DESC"));
    } else if (appearance) {
        q.prepare(
            metadata +
            QStringLiteral(
                "(SELECT descriptor FROM appearances a WHERE a.photo_id=photos.id AND a.version=? "
                "AND a.modified=photos.modified AND a.bytes=photos.bytes),? FROM photos ORDER BY "
                "modified DESC,id DESC"));
        q.addBindValue(AppearanceVersion);
        q.addBindValue(ModelVersion);
    } else {
        q.prepare(metadata +
                  QStringLiteral("embedding,model FROM photos ORDER BY modified DESC,id DESC"));
    }
    checked(q);
    Catalog c;
    if (loadVectors)
        c.vectors.reserve(size_t(count()) * Dimensions);
    while (q.next()) {
        if (!loadVectors) {
            c.photos << row(q);
            continue;
        }
        const auto b = q.value(13).toByteArray();
        const auto model = q.value(14).toString();
        const size_t old = c.vectors.size();
        c.vectors.resize(old + Dimensions, 0.f);
        bool usable =
            (model == ModelVersion || model == GpuModelVersion ||
             model == CompatiblePreviousModelVersion || model == CompatibleLegacyModelVersion) &&
            b.size() == Dimensions * int(sizeof(float));
        if (usable) {
            auto *vector = c.vectors.data() + old;
            memcpy(vector, b.constData(), Dimensions * sizeof(float));
            double squaredNorm = 0;
            for (int i = 0; i < Dimensions; ++i) {
                if (!std::isfinite(vector[i])) {
                    usable = false;
                    break;
                }
                squaredNorm += double(vector[i]) * vector[i];
            }
            usable = usable && squaredNorm >= 1e-12;
            if (usable) {
                const double inverseNorm = 1 / std::sqrt(squaredNorm);
                for (int i = 0; i < Dimensions; ++i)
                    vector[i] = float(vector[i] * inverseNorm);
            } else {
                std::fill(vector, vector + Dimensions, 0.f);
            }
        }
        c.semanticAvailable.push_back(usable);
        if (!usable)
            ++c.unavailableEmbeddings;
        else if (model != ModelVersion && model != GpuModelVersion)
            ++c.legacyEmbeddings;
        c.photos << row(q);
    }
    return c;
}
} // namespace piclocate
