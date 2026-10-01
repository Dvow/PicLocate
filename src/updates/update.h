#pragma once
#include <QtNetwork>
#include <array>
#include <memory>
#include <optional>

namespace piclocate {
struct UpdateRelease {
    QString version, name, sha256;
    QUrl url;
    qint64 size = 0;
};
struct UpdateSource {
    QString repository, platform, currentVersion;
};
std::optional<std::array<int, 3>> updateVersion(const QString &text);
std::optional<UpdateRelease> parseUpdate(const QByteArray &json, const QString &repository,
                                         const QString &platform, const QString &current);
bool updateRedirectAllowed(const QUrl &url);
bool verifyUpdateFile(const QString &path, const UpdateRelease &release);

class AppUpdate : public QObject {
    Q_OBJECT
  public:
    enum class State { Idle, Checking, Available, Downloading, Ready, Failed };
    Q_ENUM(State)
    static UpdateSource defaultSource();
    AppUpdate(QString cache, UpdateSource source, QNetworkAccessManager *network,
              QObject *parent = nullptr);
    ~AppUpdate() override;
    bool enabled() const;
    State state() const { return state_; }
    QString message() const { return message_; }
    void check();
    void download();
    void cancel();
    bool install(const QString &library, QString *error);
  signals:
    void changed();
    void progress(qint64 received, qint64 total);

  private:
    QString cache_, message_, downloaded_;
    State state_ = State::Idle;
    UpdateRelease release_;
    QNetworkAccessManager manager_;
    QNetworkAccessManager *network_ = &manager_;
    UpdateSource source_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QByteArray manifest_;
    qint64 received_ = 0;
    QString failure_;
    void setState(State state, const QString &message);
    void request(const QUrl &url, bool asset);
    void consume(bool asset);
};
} // namespace piclocate
