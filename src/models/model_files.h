#pragma once
#include <QList>
#include <QString>

namespace piclocate {
struct ModelFile {
    QString name, remote, sha256;
    qint64 size;
};
QList<ModelFile> modelFiles();
ModelFile gpuModelFile();
bool modelsPresent(const QString &directory);
} // namespace piclocate
