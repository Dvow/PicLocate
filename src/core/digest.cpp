#include "core/core.h"
#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <bcrypt.h>
// clang-format on
#endif

namespace piclocate {
QByteArray fileSha256(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
#ifdef Q_OS_WIN
    // Use the OS crypto provider for large model files, retaining the pinned
    // SHA-256 checks. Handles and the fixed-size read buffer are call-local.
    struct Handles {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        BCRYPT_HASH_HANDLE hash = nullptr;
        ~Handles() {
            if (hash)
                BCryptDestroyHash(hash);
            if (algorithm)
                BCryptCloseAlgorithmProvider(algorithm, 0);
        }
    } handles;
    if (BCryptOpenAlgorithmProvider(&handles.algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptCreateHash(handles.algorithm, &handles.hash, nullptr, 0, nullptr, 0, 0) < 0)
        return {};
    QByteArray buffer(1024 * 1024, Qt::Uninitialized);
    while (true) {
        const auto count = file.read(buffer.data(), buffer.size());
        if (count < 0)
            return {};
        if (count == 0)
            break;
        if (BCryptHashData(handles.hash, reinterpret_cast<PUCHAR>(buffer.data()), ULONG(count), 0) <
            0)
            return {};
    }
    QByteArray digest(32, Qt::Uninitialized);
    if (BCryptFinishHash(handles.hash, reinterpret_cast<PUCHAR>(digest.data()),
                         ULONG(digest.size()), 0) < 0)
        return {};
    return digest;
#else
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result() : QByteArray{};
#endif
}
} // namespace piclocate
