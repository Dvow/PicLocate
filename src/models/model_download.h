#pragma once
#include <QtNetwork>
#include <memory>

namespace piclocate {
class ModelDownload : public QObject {
    Q_OBJECT
  public:
    explicit ModelDownload(QObject *parent = nullptr) : QObject(parent) {}
    void start(const QString &directory);
    void cancel();
    bool running() const { return running_; }
  signals:
    void progress(qint64 received, qint64 total, QString filename);
    void finished(bool ok, QString message);

  private:
    QNetworkAccessManager manager_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QSaveFile> file_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QString directory_;
    int index_ = 0;
    qint64 completed_ = 0;
    bool running_ = false, cancelled_ = false;
    void next();
    void fail(const QString &message);
};
} // namespace piclocate
