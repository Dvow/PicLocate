#include "core/core.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#if defined(PICLOCATE_AVX2_DISPATCH) && defined(_MSC_VER)
#include <intrin.h>
#endif
#ifdef Q_OS_WIN
// clang-format off
#include <windows.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>
// clang-format on
#endif

namespace piclocate {
QString normalizedPath(const QString &p) {
    QFileInfo f(p);
    return QDir::fromNativeSeparators(f.canonicalFilePath().isEmpty() ? f.absoluteFilePath()
                                                                      : f.canonicalFilePath());
}
QString fileSize(qint64 n) {
    if (n >= 1024 * 1024 * 1024)
        return QString::number(double(n) / (1024 * 1024 * 1024), 'f', 1) + QStringLiteral(" GB");
    if (n >= 1024 * 1024)
        return QString::number(double(n) / (1024 * 1024), 'f', 1) + QStringLiteral(" MB");
    return QString::number(double(n) / 1024, 'f', 0) + QStringLiteral(" KB");
}
QImage readImage(const QString &p, int max) {
    QImageReader r(p);
    r.setAutoTransform(true);
    const auto size = r.size();
    if (max > 0 && size.isValid() && qMax(size.width(), size.height()) > max)
        r.setScaledSize(size.scaled(max, max, Qt::KeepAspectRatio));
    return r.read();
}
void normalize(std::vector<float> &v) {
    double s = 0;
    for (float f : v) {
        if (!std::isfinite(f))
            throw std::runtime_error("Non-finite embedding");
        s += double(f) * f;
    }
    if (s < 1e-12)
        throw std::runtime_error("Empty embedding");
    const float inv = float(1 / std::sqrt(s));
    for (auto &f : v)
        f *= inv;
}
static float dotScalar(const float *a, const float *b, int n) {
    float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
    int i = 0;
    for (; i + 3 < n; i += 4) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
    }
    float s = (s0 + s1) + (s2 + s3);
    for (; i < n; ++i)
        s += a[i] * b[i];
    return s;
}
#ifdef PICLOCATE_AVX2_DISPATCH
float dotAvx2(const float *a, const float *b, int n);
static bool supportsAvx2() {
    if (qEnvironmentVariableIsSet("PICLOCATE_DISABLE_AVX2"))
        return false;
#ifdef _MSC_VER
    int cpu[4]{};
    __cpuid(cpu, 0);
    if (cpu[0] < 7)
        return false;
    __cpuidex(cpu, 1, 0);
    if ((cpu[2] & (1 << 27)) == 0 || (cpu[2] & (1 << 28)) == 0 || (_xgetbv(0) & 6) != 6)
        return false;
    __cpuidex(cpu, 7, 0);
    return (cpu[1] & (1 << 5)) != 0;
#else
    // GCC/Clang check CPU support and OS vector-register state support.
    return __builtin_cpu_supports("avx2");
#endif
}
#endif
QString vectorBackend() {
#ifdef PICLOCATE_AVX2_DISPATCH
    return supportsAvx2() ? QStringLiteral("AVX2") : QStringLiteral("portable CPU");
#else
    return QStringLiteral("portable CPU");
#endif
}
float dot(const float *a, const float *b, int n) {
#ifdef PICLOCATE_AVX2_DISPATCH
    static const auto implementation = supportsAvx2() ? dotAvx2 : dotScalar;
    return implementation(a, b, n);
#else
    return dotScalar(a, b, n);
#endif
}
QList<QPair<int, float>> topK(const std::vector<float> &v, const std::vector<float> &q, int k) {
    QList<QPair<int, float>> r;
    if (q.size() != Dimensions || v.size() % Dimensions || k <= 0)
        return r;
    const int n = int(v.size() / Dimensions);
    r.reserve(n);
    for (int i = 0; i < n; ++i)
        r.append({i, dot(v.data() + size_t(i) * Dimensions, q.data())});
    const int keep = qMin(k, n);
    std::partial_sort(r.begin(), r.begin() + keep, r.end(), [](auto a, auto b) {
        return a.second == b.second ? a.first < b.first : a.second > b.second;
    });
    r.resize(keep);
    return r;
}
QString recognizeText(const QImage &source) {
#ifdef Q_OS_WIN
    try {
        thread_local const bool initialized = []() {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            return true;
        }();
        Q_UNUSED(initialized);
        thread_local const auto engine =
            winrt::Windows::Media::Ocr::OcrEngine::TryCreateFromUserProfileLanguages();
        if (!engine)
            return {};
        const int limit =
            qMin(1600, int(winrt::Windows::Media::Ocr::OcrEngine::MaxImageDimension()));
        // Never turn a 128px icon into a 1600px OCR job. Retain document pixels,
        // shrink only oversized inputs, and reuse one OCR engine per worker.
        auto im = (qMax(source.width(), source.height()) > limit
                       ? source.scaled(limit, limit, Qt::KeepAspectRatio, Qt::SmoothTransformation)
                       : source)
                      .convertToFormat(QImage::Format_ARGB32);
        winrt::Windows::Storage::Streams::DataWriter writer;
        writer.WriteBytes(
            winrt::array_view<const uint8_t>(im.constBits(), im.constBits() + im.sizeInBytes()));
        using namespace winrt::Windows::Graphics::Imaging;
        auto bitmap =
            SoftwareBitmap::CreateCopyFromBuffer(writer.DetachBuffer(), BitmapPixelFormat::Bgra8,
                                                 im.width(), im.height(), BitmapAlphaMode::Ignore);
        return QString::fromStdWString(engine.RecognizeAsync(bitmap).get().Text().c_str())
            .left(16000);
    } catch (...) {
        return {};
    }
#else
    Q_UNUSED(source);
    return {};
#endif
}
} // namespace piclocate
