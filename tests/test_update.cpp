#include "updates/update.h"
#include <QtTest>
using namespace piclocate;
namespace {
class FixtureReply : public QNetworkReply {
  public:
    FixtureReply(const QNetworkRequest &request, QByteArray body, int status, NetworkError error,
                 QObject *parent)
        : QNetworkReply(parent), body_(std::move(body)) {
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        open(QIODevice::ReadOnly);
        QTimer::singleShot(10, this, [this, error] {
            if (isFinished())
                return;
            if (error != NoError)
                setError(error, QStringLiteral("Fixture network failure"));
            emit readyRead();
            if (isFinished())
                return;
            setFinished(true);
            emit finished();
        });
    }
    void abort() override {
        if (isFinished())
            return;
        setError(OperationCanceledError, QStringLiteral("Cancelled"));
        setFinished(true);
        emit finished();
    }
    qint64 bytesAvailable() const override {
        return body_.size() - position_ + QIODevice::bytesAvailable();
    }

  protected:
    qint64 readData(char *data, qint64 size) override {
        const auto count = qMin(size, qint64(body_.size()) - position_);
        if (!count)
            return -1;
        memcpy(data, body_.constData() + position_, size_t(count));
        position_ += count;
        return count;
    }

  private:
    QByteArray body_;
    qint64 position_ = 0;
};
class FixtureNetwork : public QNetworkAccessManager {
  public:
    QByteArray manifest, payload;
    QNetworkReply::NetworkError error = QNetworkReply::NoError;
    int status = 200, requests = 0;

  protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++requests;
        return new FixtureReply(
            request,
            request.url().path().endsWith(QStringLiteral("update.json")) ? manifest : payload,
            status, error, this);
    }
};
} // namespace
class UpdateTests : public QObject {
    Q_OBJECT
    static QByteArray fixture(const QString &version = QStringLiteral("1.8.0"),
                              const QString &target = QStringLiteral("linux-x64")) {
        const auto name = QStringLiteral("PicLocate-%1-%2.tar.gz").arg(version, target);
        return QJsonDocument(
                   QJsonObject{
                       {QStringLiteral("schema"), 1},
                       {QStringLiteral("tag_name"), u'v' + version},
                       {QStringLiteral("draft"), false},
                       {QStringLiteral("prerelease"), false},
                       {QStringLiteral("assets"),
                        QJsonArray{QJsonObject{
                            {QStringLiteral("name"), name},
                            {QStringLiteral("state"), QStringLiteral("uploaded")},
                            {QStringLiteral("size"), 123},
                            {QStringLiteral("digest"),
                             QStringLiteral("sha256:") + QString(64, u'a')},
                            {QStringLiteral("browser_download_url"),
                             QStringLiteral(
                                 "https://github.com/example/piclocate/releases/download/v%1/%2")
                                 .arg(version, name)}}}}})
            .toJson();
    }
    static std::optional<UpdateRelease> parse(const QByteArray &data,
                                              const QString &current = QStringLiteral("1.7.1")) {
        return parseUpdate(data, QStringLiteral("example/piclocate"), QStringLiteral("linux-x64"),
                           current);
    }
  private slots:
    void systemPackagesUseTheirPackageManager() {
        QTemporaryDir directory;
        const auto binary = directory.path() + QStringLiteral("/bin");
        const auto resources = directory.path() + QStringLiteral("/share/PicLocate");
        QVERIFY(QDir().mkpath(binary));
        QVERIFY(QDir().mkpath(resources));
        QVERIFY(systemPackageFormat(binary).isEmpty());
        QFile marker(resources + QStringLiteral("/.piclocate-system-package"));
        for (const auto &format : {QByteArrayLiteral("deb"), QByteArrayLiteral("arch")}) {
            QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
            marker.write(format + '\n');
            marker.close();
            QCOMPARE(systemPackageFormat(binary), QString::fromLatin1(format));
            FixtureNetwork network;
            AppUpdate update(directory.path(),
                             {QStringLiteral("example/piclocate"),
                              QStringLiteral("linux-x64-") + QString::fromLatin1(format),
                              QStringLiteral("1.7.1")},
                             &network);
            QVERIFY(update.systemManaged());
            QString error;
            QVERIFY(!update.install(directory.path(), &error));
            QVERIFY(
                error.contains(format == "deb" ? QStringLiteral("apt") : QStringLiteral("pacman")));
            QCOMPARE(network.requests, 0);
        }
        QVERIFY(marker.open(QIODevice::WriteOnly | QIODevice::Truncate));
        marker.write("invalid\n");
        marker.close();
        QVERIFY(systemPackageFormat(binary).isEmpty());
    }
    void packagesForEveryPlatform() {
        const QList<QPair<QString, QString>> packages{
            {QStringLiteral("windows-x64"), QStringLiteral("PicLocate-1.8.0-Setup-x64.exe")},
            {QStringLiteral("windows-arm64"), QStringLiteral("PicLocate-1.8.0-Setup-arm64.exe")},
            {QStringLiteral("linux-x64"), QStringLiteral("PicLocate-1.8.0-linux-x64.tar.gz")},
            {QStringLiteral("linux-arm64"), QStringLiteral("PicLocate-1.8.0-linux-arm64.tar.gz")},
            {QStringLiteral("linux-x64-deb"), QStringLiteral("PicLocate-1.8.0-linux-x64.deb")},
            {QStringLiteral("linux-arm64-deb"), QStringLiteral("PicLocate-1.8.0-linux-arm64.deb")},
            {QStringLiteral("linux-x64-arch"),
             QStringLiteral("PicLocate-1.8.0-linux-x64.pkg.tar.zst")},
            {QStringLiteral("macos-x64"), QStringLiteral("PicLocate-1.8.0-macos-x64.dmg")},
            {QStringLiteral("macos-arm64"), QStringLiteral("PicLocate-1.8.0-macos-arm64.dmg")}};
        for (const auto &[target, name] : packages) {
            auto root = QJsonDocument::fromJson(fixture()).object();
            auto asset = root[QStringLiteral("assets")].toArray().first().toObject();
            asset[QStringLiteral("name")] = name;
            asset[QStringLiteral("browser_download_url")] =
                QStringLiteral("https://github.com/example/piclocate/releases/download/v1.8.0/") +
                name;
            root[QStringLiteral("assets")] = QJsonArray{asset};
            const auto release =
                parseUpdate(QJsonDocument(root).toJson(), QStringLiteral("example/piclocate"),
                            target, QStringLiteral("1.7.1"));
            QVERIFY(release);
            QCOMPARE(release->name, name);
        }
    }
    void networkDownloadValidation() {
        const auto valid = QByteArrayLiteral("verified installer");
        for (const auto &payload : {valid, QByteArrayLiteral("modified installer"),
                                    QByteArray(19, 'x'), QByteArrayLiteral("short")}) {
            QTemporaryDir cache;
            FixtureNetwork network;
            auto root = QJsonDocument::fromJson(fixture()).object();
            auto asset = root[QStringLiteral("assets")].toArray().first().toObject();
            asset[QStringLiteral("size")] = valid.size();
            asset[QStringLiteral("digest")] =
                QStringLiteral("sha256:") +
                QString::fromLatin1(
                    QCryptographicHash::hash(valid, QCryptographicHash::Sha256).toHex());
            root[QStringLiteral("assets")] = QJsonArray{asset};
            network.manifest = QJsonDocument(root).toJson();
            network.payload = payload;
            AppUpdate update(cache.path(),
                             {QStringLiteral("example/piclocate"), QStringLiteral("linux-x64"),
                              QStringLiteral("1.7.1")},
                             &network);
            update.check();
            QTRY_COMPARE(update.state(), AppUpdate::State::Available);
            update.download();
            QTRY_VERIFY(update.state() != AppUpdate::State::Downloading);
            QCOMPARE(update.state(),
                     payload == valid ? AppUpdate::State::Ready : AppUpdate::State::Failed);
            const auto path = cache.filePath(QStringLiteral("PicLocate-1.8.0-linux-x64.tar.gz"));
            QCOMPARE(QFileInfo::exists(path), payload == valid);
            QCOMPARE(network.requests, 2);
        }
    }
    void cancellationAndTlsFailure() {
        QTemporaryDir cache;
        FixtureNetwork network;
        network.manifest = fixture();
        AppUpdate update(cache.path(),
                         {QStringLiteral("example/piclocate"), QStringLiteral("linux-x64"),
                          QStringLiteral("1.7.1")},
                         &network);
        update.check();
        update.cancel();
        QTRY_COMPARE(update.state(), AppUpdate::State::Failed);
        QVERIFY(update.message().contains(QStringLiteral("cancelled")));
        network.error = QNetworkReply::SslHandshakeFailedError;
        update.check();
        QTRY_COMPARE(update.state(), AppUpdate::State::Failed);
        QVERIFY(
            !QFileInfo::exists(cache.filePath(QStringLiteral("PicLocate-1.8.0-linux-x64.tar.gz"))));
        network.error = QNetworkReply::NoError;
        network.manifest = QByteArray(65537, 'x');
        update.check();
        QTRY_COMPARE(update.state(), AppUpdate::State::Failed);
        QVERIFY(update.message().contains(QStringLiteral("size")));
    }
    void versions() {
        QVERIFY(updateVersion(QStringLiteral("1.7.1")));
        for (const auto &invalid :
             {QStringLiteral("v1.7.1"), QStringLiteral("01.7.1"), QStringLiteral("1.7.1-beta"),
              QStringLiteral("1.7"), QStringLiteral("-1.7.1"), QStringLiteral("1.7.1\n"),
              QStringLiteral("1000000000.7.1")})
            QVERIFY(!updateVersion(invalid));
        QVERIFY(*updateVersion(QStringLiteral("1.10.0")) >
                *updateVersion(QStringLiteral("1.9.99")));
    }
    void newerOnly() {
        QVERIFY(parse(fixture()));
        QVERIFY(!parse(fixture(QStringLiteral("1.7.1"))));
        QVERIFY(!parse(fixture(QStringLiteral("1.7.0"))));
        QVERIFY(parse(fixture(QStringLiteral("1.10.0")), QStringLiteral("1.9.99")));
    }
    void rejectsWrongPlatformAndInvalidMetadata() {
        QVERIFY_EXCEPTION_THROWN(
            parse(fixture(QStringLiteral("1.8.0"), QStringLiteral("linux-arm64"))),
            std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(parse(QByteArrayLiteral("not json")), std::runtime_error);
        QVERIFY_EXCEPTION_THROWN(parse(QByteArray(65537, ' ')), std::runtime_error);
        for (const auto &key : {QStringLiteral("draft"), QStringLiteral("prerelease")}) {
            auto root = QJsonDocument::fromJson(fixture()).object();
            root[key] = true;
            QVERIFY_EXCEPTION_THROWN(parse(QJsonDocument(root).toJson()), std::runtime_error);
            root.remove(key);
            QVERIFY_EXCEPTION_THROWN(parse(QJsonDocument(root).toJson()), std::runtime_error);
        }
    }
    void rejectsUntrustedAssets() {
        const QList<QPair<QString, QJsonValue>> cases{
            {QStringLiteral("browser_download_url"),
             QStringLiteral("https://example.org/installer.exe")},
            {QStringLiteral("browser_download_url"),
             QStringLiteral("https://github.com/other/repo/releases/download/v1.8.0/"
                            "PicLocate-1.8.0-linux-x64.tar.gz")},
            {QStringLiteral("digest"), QStringLiteral("sha256:bad")},
            {QStringLiteral("state"), QStringLiteral("new")},
            {QStringLiteral("size"), -1},
            {QStringLiteral("size"), 0},
            {QStringLiteral("size"), 1.5},
            {QStringLiteral("size"), double(3LL * 1024 * 1024 * 1024)}};
        for (const auto &[key, value] : cases) {
            auto root = QJsonDocument::fromJson(fixture()).object();
            auto asset = root[QStringLiteral("assets")].toArray()[0].toObject();
            asset[key] = value;
            root[QStringLiteral("assets")] = QJsonArray{asset};
            QVERIFY_EXCEPTION_THROWN(parse(QJsonDocument(root).toJson()), std::runtime_error);
        }
        auto root = QJsonDocument::fromJson(fixture()).object();
        auto assets = root[QStringLiteral("assets")].toArray();
        assets.append(assets.first());
        root[QStringLiteral("assets")] = assets;
        QVERIFY_EXCEPTION_THROWN(parse(QJsonDocument(root).toJson()), std::runtime_error);
    }
    void redirectsStayOnGithubHttps() {
        QVERIFY(updateRedirectAllowed(
            QUrl(QStringLiteral("https://release-assets.githubusercontent.com/asset?token=abc"))));
        for (const auto &url :
             {QStringLiteral("http://github.com/file"),
              QStringLiteral("https://github.com.evil.com/file"),
              QStringLiteral("https://evil.com/file"),
              QStringLiteral("https://user:password@github.com/file"),
              QStringLiteral("https://github.com:8443/file"), QStringLiteral("file:///tmp/asset")})
            QVERIFY(!updateRedirectAllowed(QUrl(url)));
    }
    void verifiesDownloadedBytes() {
        QTemporaryDir directory;
        const auto path = directory.filePath(QStringLiteral("update.exe"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("verified installer"), 18);
        file.close();
        UpdateRelease release;
        release.size = 18;
        release.sha256 =
            QString::fromLatin1(QCryptographicHash::hash(QByteArrayLiteral("verified installer"),
                                                         QCryptographicHash::Sha256)
                                    .toHex());
        QVERIFY(verifyUpdateFile(path, release));
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("modified installer"), 18);
        file.close();
        QVERIFY(!verifyUpdateFile(path, release));
        release.size = 17;
        QVERIFY(!verifyUpdateFile(path, release));
    }
};
QTEST_GUILESS_MAIN(UpdateTests)
#include "test_update.moc"
