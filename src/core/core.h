#pragma once
#include "core/types.h"
#include <QtCore>
#include <QtGui>
#include <vector>

namespace piclocate {
QString defaultLibraryRoot();
QString vectorBackend();
QString normalizedPath(const QString &path);
QString fileSize(qint64 bytes);
// Empty on read/hash failure. Every call verifies the complete current file.
QByteArray fileSha256(const QString &path);
QImage readImage(const QString &path, int maxDimension = 0);
void normalize(std::vector<float> &vector);
float dot(const float *a, const float *b, int size = Dimensions);
QList<QPair<int, float>> topK(const std::vector<float> &vectors, const std::vector<float> &query,
                              int k);
QString recognizeText(const QImage &image);
} // namespace piclocate
