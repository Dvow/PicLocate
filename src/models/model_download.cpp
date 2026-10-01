#include "models/model_download.h"
#include "models/model_files.h"
namespace piclocate {
void ModelDownload::start(const QString &dir) {
    if (running_)
        return;
    directory_ = dir;
    QDir().mkpath(directory_);
    index_ = 0;
    completed_ = 0;
    running_ = true;
    cancelled_ = false;
    next();
}
void ModelDownload::cancel() {
    cancelled_ = true;
    if (reply_)
        reply_->abort();
    else if (running_)
        fail(QStringLiteral("Download cancelled. Your library is unchanged."));
}
void ModelDownload::fail(const QString &message) {
    if (file_) {
        file_->cancelWriting();
        file_.reset();
    }
    running_ = false;
    emit finished(false, message);
}
void ModelDownload::next() {
    const auto files = modelFiles();
    if (index_ >= files.size()) {
        running_ = false;
        emit finished(true, QStringLiteral("Local search models are ready."));
        return;
    }
    const auto f = files[index_];
    QFile existing(directory_ + u'/' + f.name);
    if (existing.size() == f.size && existing.open(QIODevice::ReadOnly)) {
        QCryptographicHash check(QCryptographicHash::Sha256);
        check.addData(&existing);
        if (QString::fromLatin1(check.result().toHex()) == f.sha256) {
            completed_ += f.size;
            ++index_;
            next();
            return;
        }
    }
    file_ = std::make_unique<QSaveFile>(directory_ + u'/' + f.name);
    if (!file_->open(QIODevice::WriteOnly)) {
        fail(file_->errorString());
        return;
    }
    hash_.reset();
    QNetworkRequest req(QUrl(QStringLiteral("https://huggingface.co/Xenova/clip-vit-base-patch32/"
                                            "resolve/d15189d7028b43f1d3e65039190477f6af591c2a/") +
                             f.remote));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(30000);
    reply_ = manager_.get(req);
    connect(reply_, &QNetworkReply::readyRead, this, [this] {
        if (!reply_ || !file_)
            return;
        const auto bytes = reply_->readAll();
        hash_.addData(bytes);
        if (file_->write(bytes) != bytes.size())
            reply_->abort();
    });
    connect(reply_, &QNetworkReply::downloadProgress, this, [this, f](qint64 done, qint64) {
        qint64 total = 0;
        for (const auto &m : modelFiles())
            total += m.size;
        emit progress(completed_ + done, total, f.name);
    });
    connect(reply_, &QNetworkReply::finished, this, [this, f] {
        auto *reply = reply_.data();
        reply_ = nullptr;
        const auto error = reply->error();
        const auto message = reply->errorString();
        const auto tail = reply->readAll();
        if (!tail.isEmpty()) {
            hash_.addData(tail);
            file_->write(tail);
        }
        reply->deleteLater();
        if (cancelled_) {
            fail(QStringLiteral("Download cancelled."));
            return;
        }
        if (error != QNetworkReply::NoError) {
            fail(QStringLiteral("Download failed: ") + message);
            return;
        }
        if (file_->size() != f.size || QString::fromLatin1(hash_.result().toHex()) != f.sha256) {
            fail(QStringLiteral("Model checksum did not match. Download rejected."));
            return;
        }
        if (!file_->commit()) {
            fail(QStringLiteral("Could not save the model."));
            return;
        }
        file_.reset();
        completed_ += f.size;
        ++index_;
        next();
    });
}
} // namespace piclocate
