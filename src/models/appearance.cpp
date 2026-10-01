#include "models/appearance.h"
#include <cmath>

namespace piclocate {
int projectAppearance(const float *source, float *destination, SimilarityMode mode) {
    const bool focused = mode == SimilarityMode::Shape || mode == SimilarityMode::Color;
    if (!focused) {
        memmove(destination, source, Dimensions * sizeof(float));
        return Dimensions;
    }
    const int first = mode == SimilarityMode::Shape ? 192 : 0;
    const int firstLength = mode == SimilarityMode::Shape ? 64 : 192;
    const int second = mode == SimilarityMode::Shape ? 320 : 256;
    memmove(destination, source + first, firstLength * sizeof(float));
    memmove(destination + firstLength, source + second, (256 - firstLength) * sizeof(float));
    const float norm = dot(destination, destination, 256);
    if (norm > 1e-12f) {
        const float scale = 1.f / std::sqrt(norm);
        for (int i = 0; i < 256; ++i)
            destination[i] *= scale;
    } else {
        std::fill(destination, destination + 256, 0.f);
    }
    return 256;
}
float appearanceSimilarity(const float *a, const float *b, SimilarityMode mode) {
    if (mode != SimilarityMode::Shape && mode != SimilarityMode::Color)
        return dot(a, b);
    const int starts[] = {mode == SimilarityMode::Shape ? 192 : 0,
                          mode == SimilarityMode::Shape ? 320 : 256};
    const int lengths[] = {mode == SimilarityMode::Shape ? 64 : 192,
                           mode == SimilarityMode::Shape ? 192 : 64};
    float product = 0, normA = 0, normB = 0;
    for (int i = 0; i < 2; ++i) {
        product += dot(a + starts[i], b + starts[i], lengths[i]);
        normA += dot(a + starts[i], a + starts[i], lengths[i]);
        normB += dot(b + starts[i], b + starts[i], lengths[i]);
    }
    return normA > 1e-12f && normB > 1e-12f ? product / std::sqrt(normA * normB) : 0.f;
}
std::vector<float> appearanceDescriptor(const QImage &source) {
    if (source.isNull())
        throw std::runtime_error("Cannot describe an empty image");
    auto image = source.convertToFormat(QImage::Format_ARGB32);
    // Remove transparent padding, but retain the object's aspect ratio. Hidden RGB
    // values in transparent pixels must never change its appearance descriptor.
    if (source.hasAlphaChannel()) {
        int left = image.width(), top = image.height(), right = -1, bottom = -1;
        for (int y = 0; y < image.height(); ++y) {
            const auto *pixels = reinterpret_cast<const QRgb *>(image.constScanLine(y));
            for (int x = 0; x < image.width(); ++x)
                if (qAlpha(pixels[x]) > 8) {
                    left = qMin(left, x);
                    right = qMax(right, x);
                    top = qMin(top, y);
                    bottom = qMax(bottom, y);
                }
        }
        if (right >= left)
            image = image.copy(left, top, right - left + 1, bottom - top + 1);
    }
    image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied)
                .scaled(32, 32, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QImage canvas(32, 32, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    {
        QPainter painter(&canvas);
        painter.drawImage((32 - image.width()) / 2, (32 - image.height()) / 2, image);
    }
    const auto small = canvas.scaled(8, 8, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    std::vector<float> result(Dimensions, 0.f);
    float luminance[32][32]{};
    for (int y = 0; y < 32; ++y) {
        const auto *pixels = reinterpret_cast<const QRgb *>(canvas.constScanLine(y));
        for (int x = 0; x < 32; ++x) {
            const QRgb p = pixels[x];
            const float alpha = qAlpha(p) / 255.f;
            luminance[y][x] = (.2126f * qRed(p) + .7152f * qGreen(p) + .0722f * qBlue(p)) / 255.f +
                              .5f * (1 - alpha);
            if (alpha > .03f) {
                const auto straight = qUnpremultiply(p);
                const int bin =
                    (qRed(straight) / 64) * 16 + (qGreen(straight) / 64) * 4 + qBlue(straight) / 64;
                result[256 + bin] += alpha;
            }
        }
    }
    float mean = 0;
    for (int y = 0; y < 8; ++y) {
        const auto *pixels = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < 8; ++x) {
            const int i = y * 8 + x;
            const QRgb p = pixels[x];
            result[i * 3] = qRed(p) / 255.f;
            result[i * 3 + 1] = qGreen(p) / 255.f;
            result[i * 3 + 2] = qBlue(p) / 255.f;
            result[192 + i] = qAlpha(p) / 255.f;
            result[448 + i] = qGray(p) / 255.f + .5f * (1 - result[192 + i]);
            mean += result[448 + i] / 64.f;
        }
    }
    for (int i = 448; i < Dimensions; ++i)
        result[i] -= mean;
    // Spatial gradient histograms retain silhouette and internal edge directions.
    constexpr float pi = 3.14159265358979323846f;
    for (int y = 1; y < 31; ++y)
        for (int x = 1; x < 31; ++x) {
            const float dx = luminance[y][x + 1] - luminance[y][x - 1];
            const float dy = luminance[y + 1][x] - luminance[y - 1][x];
            const float magnitude = std::sqrt(dx * dx + dy * dy);
            const float angle = (std::atan2(dy, dx) + pi) * (8.f / (2 * pi));
            const int bin = int(angle) % 8;
            const float fraction = angle - std::floor(angle);
            const int offset = 320 + ((y / 8) * 4 + x / 8) * 8;
            result[offset + bin] += magnitude * (1 - fraction);
            result[offset + (bin + 1) % 8] += magnitude * fraction;
        }
    const int boundaries[] = {0, 192, 256, 320, 448, 512};
    const float weights[] = {.30f, .15f, .20f, .20f, .15f};
    for (int group = 0; group < 5; ++group) {
        double norm = 0;
        for (int i = boundaries[group]; i < boundaries[group + 1]; ++i)
            norm += double(result[i]) * result[i];
        if (norm > 1e-12) {
            const float scale = float(std::sqrt(weights[group] / norm));
            for (int i = boundaries[group]; i < boundaries[group + 1]; ++i)
                result[i] *= scale;
        }
    }
    // Fully transparent images are valid assets too.
    if (dot(result.data(), result.data()) < 1e-12f)
        result[192] = 1;
    normalize(result);
    return result;
}
} // namespace piclocate
