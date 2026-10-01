#include "updates/update.h"
#include <QProcess>
#include <stdexcept>

namespace piclocate {
namespace {
constexpr qint64 MaxManifest = 65536, MaxPackage = 2LL * 1024 * 1024 * 1024;
QString packageName(const QString &version, const QString &target) {
    const auto arch = target.section(u'-', 1);
    if (target.startsWith(QStringLiteral("windows-")))
        return QStringLiteral("PicLocate-%1-Setup-%2.exe").arg(version, arch);
    return QStringLiteral("PicLocate-%1-%2.%3")
        .arg(version, target,
             target.startsWith(QStringLiteral("macos-")) ? QStringLiteral("dmg")
                                                         : QStringLiteral("tar.gz"));
}
} // namespace
std::optional<std::array<int, 3>> updateVersion(const QString &text) {
    static const QRegularExpression valid(
        QStringLiteral("\\A(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\z"));
    const auto match = valid.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    return std::array<int, 3>{match.captured(1).toInt(), match.captured(2).toInt(),
                              match.captured(3).toInt()};
}
std::optional<UpdateRelease> parseUpdate(const QByteArray &json, const QString &repo,
                                         const QString &target, const QString &current) {
    if (json.size() > MaxManifest || !updateVersion(current))
        throw std::runtime_error("Invalid update metadata.");
    const auto doc = QJsonDocument::fromJson(json);
    const auto root = doc.object();
    const auto tag = root.value(QStringLiteral("tag_name")).toString();
    const auto version = tag.startsWith(u'v') ? tag.mid(1) : QString();
    const auto parsed = updateVersion(version);
    if (!doc.isObject() || root.value(QStringLiteral("schema")).toInt() != 1 || !parsed ||
        root.value(QStringLiteral("draft")) != QJsonValue(false) ||
        root.value(QStringLiteral("prerelease")) != QJsonValue(false) ||
        !root.value(QStringLiteral("assets")).isArray())
        throw std::runtime_error("Invalid update metadata.");
    const auto name = packageName(version, target);
    std::optional<UpdateRelease> found;
    for (const auto &value : root.value(QStringLiteral("assets")).toArray()) {
        const auto asset = value.toObject();
        if (asset.value(QStringLiteral("name")).toString() != name)
            continue;
        const auto digest = asset.value(QStringLiteral("digest")).toString();
        const auto size = asset.value(QStringLiteral("size")).toDouble(-1);
        const auto expected =
            QStringLiteral("https://github.com/%1/releases/download/%2/%3").arg(repo, tag, name);
        static const QRegularExpression sha(QStringLiteral("\\Asha256:[a-f0-9]{64}\\z"));
        if (found ||
            asset.value(QStringLiteral("state")).toString() != QStringLiteral("uploaded") ||
            !sha.match(digest).hasMatch() || size <= 0 || size > MaxPackage ||
            size != qint64(size) ||
            asset.value(QStringLiteral("browser_download_url")).toString() != expected)
            throw std::runtime_error("Invalid update package.");
        found = UpdateRelease{version, name, digest.mid(7), QUrl(expected), qint64(size)};
    }
    if (!found)
        throw std::runtime_error("This release has no package for this platform.");
    return *parsed > *updateVersion(current) ? found : std::nullopt;
}
bool updateRedirectAllowed(const QUrl &url) {
    const auto host = url.host().toLower();
    return url.isValid() && url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty() &&
           (url.port() == -1 || url.port() == 443) &&
           (host == QStringLiteral("github.com") ||
            host == QStringLiteral("release-assets.githubusercontent.com") ||
            host == QStringLiteral("objects.githubusercontent.com"));
}
bool verifyUpdateFile(const QString &path, const UpdateRelease &release) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.size() == release.size && file.open(QIODevice::ReadOnly) && hash.addData(&file) &&
           QString::fromLatin1(hash.result().toHex()) == release.sha256;
}
UpdateSource AppUpdate::defaultSource() {
    return {QString::fromLatin1(PICLOCATE_UPDATE_REPOSITORY),
            QString::fromLatin1(PICLOCATE_PLATFORM_KEY), QString::fromLatin1(PICLOCATE_VERSION)};
}
AppUpdate::AppUpdate(QString cache, UpdateSource source, QNetworkAccessManager *network,
                     QObject *parent)
    : QObject(parent), cache_(std::move(cache)), network_(network ? network : &manager_),
      source_(std::move(source)) {}
AppUpdate::~AppUpdate() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
}
bool AppUpdate::enabled() const {
    return !source_.repository.isEmpty();
}
void AppUpdate::setState(State state, const QString &message) {
    state_ = state;
    message_ = message;
    emit changed();
}
void AppUpdate::check() {
    if (!enabled() || reply_ || state_ == State::Ready)
        return;
    manifest_.clear();
    setState(State::Checking, QStringLiteral("Checking for updates…"));
    request(QUrl(QStringLiteral("https://github.com/%1/releases/latest/download/update.json")
                     .arg(source_.repository)),
            false);
}
void AppUpdate::download() {
    if (state_ != State::Available || reply_)
        return;
    if (!QDir().mkpath(cache_)) {
        setState(State::Failed, QStringLiteral("Could not create the update folder."));
        return;
    }
    downloaded_ = cache_ + u'/' + release_.name;
    if (verifyUpdateFile(downloaded_, release_)) {
        setState(State::Ready, QStringLiteral("Update ready to install."));
        return;
    }
    file_ = std::make_unique<QSaveFile>(downloaded_);
    if (!file_->open(QIODevice::WriteOnly)) {
        setState(State::Failed, file_->errorString());
        file_.reset();
        return;
    }
    hash_.reset();
    setState(State::Downloading, QStringLiteral("Downloading PicLocate %1…").arg(release_.version));
    request(release_.url, true);
}
void AppUpdate::cancel() {
    if (reply_) {
        failure_ = QStringLiteral("Update cancelled.");
        reply_->abort();
    }
}
void AppUpdate::consume(bool asset) {
    if (!reply_ || !failure_.isEmpty())
        return;
    const auto bytes = reply_->readAll();
    received_ += bytes.size();
    if (received_ > (asset ? release_.size : MaxManifest))
        failure_ = QStringLiteral("Update download exceeds its expected size.");
    else if (asset) {
        hash_.addData(bytes);
        if (file_->write(bytes) != bytes.size())
            failure_ = QStringLiteral("Could not save the update.");
    } else
        manifest_.append(bytes);
    if (!failure_.isEmpty())
        reply_->abort();
}
void AppUpdate::request(const QUrl &url, bool asset) {
    failure_.clear();
    received_ = 0;
    QNetworkRequest request(url);
    request.setTransferTimeout(30000);
    request.setMaximumRedirectsAllowed(5);
    request.setRawHeader("User-Agent", "PicLocate/" PICLOCATE_VERSION);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);
    reply_ = network_->get(request);
    reply_->setReadBufferSize(1024 * 1024);
    connect(reply_, &QNetworkReply::redirected, this, [this](const QUrl &next) {
        if (updateRedirectAllowed(next))
            reply_->redirectAllowed();
        else {
            failure_ = QStringLiteral("Update redirect was rejected.");
            reply_->abort();
        }
    });
    connect(reply_, &QNetworkReply::metaDataChanged, this, [this, asset] {
        const auto length = reply_->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (length > (asset ? release_.size : MaxManifest)) {
            failure_ = QStringLiteral("Update download exceeds its expected size.");
            reply_->abort();
        }
    });
    connect(reply_, &QNetworkReply::readyRead, this, [this, asset] { consume(asset); });
    connect(reply_, &QNetworkReply::downloadProgress, this,
            [this, asset](qint64 done, qint64 total) {
                if (asset)
                    emit progress(done, total);
            });
    connect(reply_, &QNetworkReply::finished, this, [this, asset] {
        consume(asset);
        auto *reply = reply_.data();
        reply_ = nullptr;
        const auto error = reply->error();
        const auto code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto errorText = reply->errorString();
        reply->deleteLater();
        if (failure_.isEmpty() && (error != QNetworkReply::NoError || code != 200))
            failure_ =
                code == 404 ? QStringLiteral("No published update is available yet.") : errorText;
        if (asset && failure_.isEmpty() &&
            (received_ != release_.size ||
             QString::fromLatin1(hash_.result().toHex()) != release_.sha256))
            failure_ = QStringLiteral("Update checksum did not match. Download rejected.");
        if (asset && failure_.isEmpty() && !file_->commit())
            failure_ = QStringLiteral("Could not save the update.");
        file_.reset();
        if (!failure_.isEmpty()) {
            setState(State::Failed, failure_);
            return;
        }
        if (asset) {
            setState(State::Ready, QStringLiteral("Update ready to install."));
            return;
        }
        try {
            const auto update = parseUpdate(manifest_, source_.repository, source_.platform,
                                            source_.currentVersion);
            if (update) {
                release_ = *update;
                setState(State::Available,
                         QStringLiteral("PicLocate %1 is available.").arg(release_.version));
            } else
                setState(State::Idle, QStringLiteral("PicLocate is up to date."));
        } catch (const std::exception &e) {
            setState(State::Failed, QString::fromUtf8(e.what()));
        }
    });
}
bool AppUpdate::install(const QString &library, QString *error) {
    if (state_ != State::Ready || !verifyUpdateFile(downloaded_, release_)) {
        *error = QStringLiteral("The update is missing or damaged. Download it again.");
        setState(State::Available, *error);
        return false;
    }
    QProcess process;
#ifdef Q_OS_WIN
    process.setProgram(downloaded_);
    process.setArguments({QStringLiteral("/SILENT"), QStringLiteral("/NORESTART"),
                          QStringLiteral("/SP-"),
                          QStringLiteral("/UPDATEPID=%1").arg(QCoreApplication::applicationPid()),
                          QStringLiteral("/UPDATEDATA=%1").arg(library),
                          QStringLiteral("/DIR=%1").arg(QCoreApplication::applicationDirPath())});
#else
    const auto binary = QCoreApplication::applicationDirPath();
#ifdef Q_OS_MACOS
    const auto root = QDir(binary + QStringLiteral("/../..")).canonicalPath();
    const auto resources = root + QStringLiteral("/Contents/Resources");
#else
    const auto root = QDir(binary + QStringLiteral("/..")).canonicalPath();
    const auto resources = root + QStringLiteral("/share/PicLocate");
    if (!QFileInfo(root).fileName().startsWith(QStringLiteral("PicLocate"))) {
        *error = QStringLiteral("Only dedicated PicLocate package folders can update "
                                "automatically. Install this update manually.");
        return false;
    }
#endif
    const auto data = QDir(library).canonicalPath();
    if (data == root || data.startsWith(root + u'/')) {
        *error = QStringLiteral(
            "This library is inside the app folder. Update this installation manually.");
        return false;
    }
    QFile marker(resources + QStringLiteral("/.piclocate-package"));
    if (!marker.open(QIODevice::ReadOnly) ||
        QString::fromUtf8(marker.readAll()).trimmed() != source_.platform ||
        !QFileInfo(QFileInfo(root).absolutePath()).isWritable() || !QFileInfo(root).isWritable()) {
        *error = QStringLiteral(
                     "This installation cannot update itself. Use the downloaded package in %1.")
                     .arg(cache_);
        return false;
    }
    process.setProgram(QStringLiteral("/bin/sh"));
    process.setArguments({resources + QStringLiteral("/update-unix.sh"), source_.platform,
                          downloaded_, root, QString::number(QCoreApplication::applicationPid()),
                          library});
#endif
    process.setWorkingDirectory(cache_);
    process.setStandardOutputFile(cache_ + QStringLiteral("/install.log"), QIODevice::Append);
    process.setStandardErrorFile(cache_ + QStringLiteral("/install.log"), QIODevice::Append);
    if (!process.startDetached()) {
        *error = QStringLiteral("Could not start the update installer.");
        return false;
    }
    return true;
}
} // namespace piclocate
