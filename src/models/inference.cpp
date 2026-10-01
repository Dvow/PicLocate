#include "models/inference.h"
#include <cmath>
#include <stdexcept>
namespace piclocate {
void Tokenizer::load(const QString &path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        throw std::runtime_error("Tokenizer file could not be opened");
    const auto root = QJsonDocument::fromJson(f.readAll()).object();
    const auto model = root.value(QStringLiteral("model")).toObject();
    const auto vocab = model.value(QStringLiteral("vocab")).toObject();
    for (auto i = vocab.begin(); i != vocab.end(); ++i)
        vocabulary_.insert(i.key(), i.value().toInt());
    const auto merges = model.value(QStringLiteral("merges")).toArray();
    int rank = 0;
    for (const auto &m : merges) {
        const QString key = m.isArray()
                                ? m.toArray()[0].toString() + u' ' + m.toArray()[1].toString()
                                : m.toString();
        ranks_.insert(key, rank++);
    }
    int extra = 0;
    for (int b = 0; b < 256; ++b) {
        const bool plain =
            (b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255);
        bytes_.insert(b, QChar(plain ? b : 256 + extra++));
    }
    if (vocabulary_.size() != 49408 || ranks_.isEmpty())
        throw std::runtime_error("Unsupported tokenizer vocabulary");
}
QStringList Tokenizer::bpe(const QString &token) const {
    if (cache_.contains(token))
        return cache_.value(token);
    QStringList parts;
    for (QChar c : token)
        parts << QString(c);
    if (parts.isEmpty())
        return parts;
    parts.last() += QStringLiteral("</w>");
    while (parts.size() > 1) {
        int rank = INT_MAX, at = -1;
        for (qsizetype i = 0; i + 1 < parts.size(); ++i) {
            int r = ranks_.value(parts[i] + u' ' + parts[i + 1], INT_MAX);
            if (r < rank) {
                rank = r;
                at = int(i);
            }
        }
        if (at < 0)
            break;
        const QString first = parts[at], second = parts[at + 1];
        QStringList merged;
        for (qsizetype i = 0; i < parts.size(); ++i) {
            if (i + 1 < parts.size() && parts[i] == first && parts[i + 1] == second) {
                merged << parts[i] + parts[i + 1];
                ++i;
            } else
                merged << parts[i];
        }
        parts = merged;
    }
    if (cache_.size() > 8192)
        cache_.clear();
    cache_.insert(token, parts);
    return parts;
}
std::vector<int64_t> Tokenizer::encode(const QString &source) const {
    static const QRegularExpression pattern(
        QStringLiteral("<\\|startoftext\\|>|<\\|endoftext\\|>|'s|'t|'re|'ve|'m|'ll|'d|[\\p{L}]+|["
                       "\\p{N}]|[^\\s\\p{L}\\p{N}]+"),
        QRegularExpression::UseUnicodePropertiesOption);
    const QString text = source.normalized(QString::NormalizationForm_C).simplified().toLower();
    std::vector<int64_t> ids{49406};
    auto it = pattern.globalMatch(text);
    while (it.hasNext() && ids.size() < 76) {
        const auto word = it.next().captured();
        if (word == QStringLiteral("<|startoftext|>")) {
            ids.push_back(49406);
            continue;
        }
        if (word == QStringLiteral("<|endoftext|>")) {
            ids.push_back(49407);
            continue;
        }
        QString encoded;
        const auto utf8 = word.toUtf8();
        for (unsigned char b : utf8)
            encoded += bytes_.value(b);
        for (const auto &piece : bpe(encoded)) {
            if (ids.size() >= 76)
                break;
            ids.push_back(vocabulary_.value(piece, 49407));
        }
    }
    ids.push_back(49407);
    ids.resize(77, 49407);
    return ids;
}
Inference::Inference(const QString &dir) : directory_(dir) {
    env_.DisableTelemetryEvents();
    for (const auto &f : modelFiles()) {
        if (f.name.startsWith(QStringLiteral("vision_model")))
            continue;
        const auto digest = fileSha256(dir + u'/' + f.name);
        if (digest.isEmpty())
            throw std::runtime_error(
                "Local search models are missing. Open Settings to download them.");
        if (QString::fromLatin1(digest.toHex()) != f.sha256)
            throw std::runtime_error(
                "Model integrity check failed. Remove the model pack and download again.");
    }
    options_.SetIntraOpNumThreads(qBound(1, QThread::idealThreadCount() / 3, 4));
    options_.SetInterOpNumThreads(1);
    options_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
#ifdef Q_OS_WIN
    text_ = std::make_unique<Ort::Session>(
        env_, (dir + QStringLiteral("/text_model_quantized.onnx")).toStdWString().c_str(),
        options_);
#else
    text_ = std::make_unique<Ort::Session>(
        env_, (dir + QStringLiteral("/text_model_quantized.onnx")).toUtf8().constData(), options_);
#endif
    tokenizer_.load(dir + QStringLiteral("/tokenizer.json"));
}
void Inference::loadVision() {
    if (vision_)
        return;
    const auto path = directory_ + QStringLiteral("/vision_model_quantized.onnx");
    if (QString::fromLatin1(fileSha256(path).toHex()) != modelFiles()[1].sha256)
        throw std::runtime_error(
            "Image model missing or damaged. Verify the model pack in Settings.");
#ifdef Q_OS_WIN
    vision_ = std::make_unique<Ort::Session>(env_, path.toStdWString().c_str(), options_);
#else
    vision_ = std::make_unique<Ort::Session>(env_, path.toUtf8().constData(), options_);
#endif
}
std::vector<float> Inference::extract(Ort::Value &out) {
    const auto info = out.GetTensorTypeAndShapeInfo();
    if (info.GetElementCount() != Dimensions)
        throw std::runtime_error("Unexpected model output shape");
    const float *p = out.GetTensorData<float>();
    std::vector<float> v(p, p + Dimensions);
    normalize(v);
    return v;
}
std::vector<float> Inference::text(const QString &s) {
    if (textCache_.contains(s))
        return textCache_.value(s);
    auto ids = tokenizer_.encode(s);
    std::vector<int64_t> mask(77, 0);
    bool ended = false;
    for (int i = 0; i < 77; ++i) {
        if (!ended)
            mask[i] = 1;
        if (ids[size_t(i)] == 49407)
            ended = true;
    }
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 2> shape{1, 77};
    std::vector<Ort::Value> inputs;
    inputs.push_back(Ort::Value::CreateTensor<int64_t>(memory, ids.data(), ids.size(), shape.data(),
                                                       shape.size()));
    const char *names[] = {"input_ids", "attention_mask"};
    if (text_->GetInputCount() > 1)
        inputs.push_back(Ort::Value::CreateTensor<int64_t>(memory, mask.data(), mask.size(),
                                                           shape.data(), shape.size()));
    const char *outputs[] = {"text_embeds"};
    auto values =
        text_->Run(Ort::RunOptions{nullptr}, names, inputs.data(), inputs.size(), outputs, 1);
    auto v = extract(values[0]);
    if (textCache_.size() > 256)
        textCache_.clear();
    textCache_.insert(s, v);
    return v;
}
// Separable antialiased bicubic resampling, followed by CLIP's centered 224px crop.
// Sampling uses pixel centers and widens the filter when shrinking, as Pillow does.
static double cubic(double x) {
    x = std::abs(x);
    if (x < 1)
        return 1.5 * x * x * x - 2.5 * x * x + 1;
    if (x < 2)
        return -.5 * x * x * x + 2.5 * x * x - 4 * x + 2;
    return 0;
}
struct Weights {
    int first;
    std::vector<int32_t> weights;
};
static std::vector<Weights> weights(int src, qint64 scaled, qint64 offset) {
    std::vector<Weights> out;
    out.reserve(224);
    const double scale = double(src) / scaled, filter = qMax(1., scale);
    for (int i = 0; i < 224; ++i) {
        double center = (i + offset + .5) * scale;
        int first = qMax(0, int(std::floor(center - 2 * filter + .5)));
        int last = qMin(src, int(std::floor(center + 2 * filter + .5)));
        Weights w{first, {}};
        double total = 0;
        std::vector<double> exact;
        for (int j = first; j < last; ++j) {
            const double v = cubic((j - center + .5) / filter);
            exact.push_back(v);
            total += v;
        }
        for (double v : exact) {
            v = v / total * (1 << 22);
            w.weights.push_back(int32_t(v + (v < 0 ? -.5 : .5)));
        }
        out.push_back(std::move(w));
    }
    return out;
}
std::vector<float> Inference::preprocess(const QImage &source) {
    if (source.isNull())
        throw std::runtime_error("Cannot embed an empty image");
    QImage prepared = source;
    if (source.hasAlphaChannel()) {
        const auto rgba = source.convertToFormat(QImage::Format_ARGB32);
        bool transparent = false;
        int left = rgba.width(), top = rgba.height(), right = -1, bottom = -1;
        for (int y = 0; y < rgba.height(); ++y) {
            const auto *pixels = reinterpret_cast<const QRgb *>(rgba.constScanLine(y));
            for (int x = 0; x < rgba.width(); ++x) {
                transparent |= qAlpha(pixels[x]) != 255;
                if (qAlpha(pixels[x]) > 8) {
                    left = qMin(left, x);
                    right = qMax(right, x);
                    top = qMin(top, y);
                    bottom = qMax(bottom, y);
                }
            }
        }
        if (transparent) {
            // Keep the entire sprite on a neutral matte. Dropping alpha exposed
            // invisible RGB data, while CLIP's center crop could cut off a weapon.
            prepared = QImage(224, 224, QImage::Format_RGB888);
            prepared.fill(QColor(128, 128, 128));
            if (right >= left) {
                const QRect bounds(left, top, right - left + 1, bottom - top + 1);
                const auto size = bounds.size().scaled(208, 208, Qt::KeepAspectRatio);
                QPainter painter(&prepared);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                painter.drawImage(QRect((224 - size.width()) / 2, (224 - size.height()) / 2,
                                        size.width(), size.height()),
                                  rgba, bounds);
            }
        }
    }
    const auto im = prepared.convertToFormat(QImage::Format_RGB888);
    const int sw = im.width(), sh = im.height();
    // Both resize passes quantize to bytes, so each channel has only 256
    // possible normalized values. Preserve the original float operations once
    // per value instead of repeating two divisions for every output pixel.
    static const auto normalized = [] {
        constexpr float mean[] = {.48145466f, .4578275f, .40821073f},
                        std[] = {.26862954f, .26130258f, .27577711f};
        std::array<std::array<float, 256>, 3> values{};
        for (size_t c = 0; c < values.size(); ++c)
            for (size_t value = 0; value < values[c].size(); ++value) {
                const float pixel = float(value) / 255;
                values[c][value] = (pixel - mean[c]) / std[c];
            }
        return values;
    }();
    std::vector<float> out(3 * 224 * 224);
    if (sw == 224 && sh == 224) {
        // Transparent assets are already composed at the model input size.
        // Bicubic weights at this scale form the identity; avoid both passes.
        for (int y = 0; y < 224; ++y) {
            const auto *row = im.constScanLine(y);
            for (int x = 0; x < 224; ++x) {
                const size_t offset = size_t(y) * 224 + x;
                out[offset] = normalized[0][row[x * 3]];
                out[224 * 224 + offset] = normalized[1][row[x * 3 + 1]];
                out[2 * 224 * 224 + offset] = normalized[2][row[x * 3 + 2]];
            }
        }
        return out;
    }
    const double scale = 224. / qMin(sw, sh);
    // Very thin images can have virtual resized dimensions above INT_MAX even
    // when their actual bitmap is small. Only the centered crop is materialized.
    const qint64 dw = qint64(sw * scale), dh = qint64(sh * scale);
    const auto wx = weights(sw, dw, (dw - 224) / 2), wy = weights(sh, dh, (dh - 224) / 2);
    // Only rows contributing to the crop are resampled. Quantize the intermediate
    // pass to match 8-bit RGB resizing rather than retaining hidden extra precision.
    const int firstRow = wy.front().first;
    const int lastRow = wy.back().first + int(wy.back().weights.size());
    std::vector<unsigned char> temp(size_t(lastRow - firstRow) * 224 * 3);
    for (int y = firstRow; y < lastRow; ++y) {
        const auto *row = im.constScanLine(y);
        for (int x = 0; x < 224; ++x) {
            for (int c = 0; c < 3; ++c) {
                int64_t sum = 1 << 21;
                const auto &w = wx[size_t(x)];
                for (size_t j = 0; j < w.weights.size(); ++j)
                    sum += int64_t(row[(w.first + int(j)) * 3 + c]) * w.weights[j];
                temp[(size_t(y - firstRow) * 224 + x) * 3 + c] =
                    static_cast<unsigned char>(qBound(0, int(sum >> 22), 255));
            }
        }
    }
    for (int y = 0; y < 224; ++y)
        for (int x = 0; x < 224; ++x)
            for (int c = 0; c < 3; ++c) {
                int64_t sum = 1 << 21;
                const auto &w = wy[size_t(y)];
                for (size_t j = 0; j < w.weights.size(); ++j)
                    sum += int64_t(temp[((size_t(w.first - firstRow) + j) * 224 + x) * 3 + c]) *
                           w.weights[j];
                out[size_t(c) * 224 * 224 + y * 224 + x] =
                    normalized[c][qBound(0, int(sum >> 22), 255)];
            }
    return out;
}
std::vector<float> Inference::image(const QImage &im) {
    auto pixels = preprocess(im);
    return imagePixels(pixels);
}
std::vector<float> Inference::imagePixels(std::vector<float> &pixels) {
    if (pixels.size() != 3 * 224 * 224)
        throw std::runtime_error("Invalid image tensor dimensions");
    loadVision();
    auto memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    std::array<int64_t, 4> shape{1, 3, 224, 224};
    auto input = Ort::Value::CreateTensor<float>(memory, pixels.data(), pixels.size(), shape.data(),
                                                 shape.size());
    const char *names[] = {"pixel_values"}, *outputs[] = {"image_embeds"};
    auto values = vision_->Run(Ort::RunOptions{nullptr}, names, &input, 1, outputs, 1);
    return extract(values[0]);
}
void Inference::enableGpu() {
    if (gpu_)
        return;
#ifndef Q_OS_WIN
    // This release has a portable CPU backend and an optional Windows DirectML backend.
    // Do not read/hash a Windows-only GPU model on other operating systems.
    gpuFailure_ = QStringLiteral("Using the portable CPU backend on this platform");
    return;
#endif
    try {
        const auto model = gpuModelFile();
        const auto path = directory_ + u'/' + model.name;
        if (QString::fromLatin1(fileSha256(path).toHex()) != model.sha256)
            throw std::runtime_error("GPU model missing or damaged; using the verified CPU model");
        gpu_ = std::make_unique<GpuVision>(path);
        gpuFailure_.clear();
    } catch (const std::exception &e) {
        gpu_.reset();
        gpuFailure_ = QString::fromUtf8(e.what());
    }
}
std::vector<std::vector<float>> Inference::imageBatch(std::vector<std::vector<float>> &pixels) {
    if (pixels.empty())
        return {};
    if (gpu_) {
        try {
            return gpu_->images(pixels);
        } catch (const std::exception &e) {
            gpuFailure_ = QString::fromUtf8(e.what());
            gpu_.reset(); // Device loss/unsupported operations must not lose this batch.
        }
    }
    std::vector<std::vector<float>> result;
    for (auto &image : pixels)
        result.push_back(imagePixels(image));
    return result;
}
QString Inference::labels(const std::vector<float> &v) {
    if (labelNames_.isEmpty()) {
        labelNames_ =
            QStringLiteral(
                "a person|a group of people|a portrait|a child|a family|a wedding|a concert|a "
                "sports event|an astronaut|a rocket|outer space|a dog|a cat|a bird|a "
                "horse|wildlife|an insect|flowers|a tree|a forest|a mountain|a beach|the ocean|a "
                "lake|a river|a waterfall|snow|a desert|a sunset|the night sky|a city|a street|a "
                "building|a house|an interior room|a kitchen|a bedroom|an office|a car|a "
                "motorcycle|a bicycle|an airplane|a train|a boat|food|a cup of coffee|a drink|a "
                "dessert|fruit|vegetables|a computer|a phone|a camera|electronics|a book|a "
                "document|a receipt|a screenshot|a website|a chart|a map|a handwritten note|a "
                "drawing|a painting|a cartoon|anime|a logo|a "
                "product|clothing|shoes|jewelry|furniture|a garden|a bridge|a road|a farm|a "
                "swimming pool|a reflection|a texture|a pattern|a toy|a game|a colorful abstract "
                "design|a black and white photo|an aerial view|a close-up photo|an underwater "
                "scene|a rainy scene|a dark scene|a bright sunny scene")
                .split(u'|');
        for (const auto &name : labelNames_) {
            auto t = text(QStringLiteral("a photo of ") + name);
            labelVectors_.insert(labelVectors_.end(), t.begin(), t.end());
        }
    }
    QStringList names;
    const auto matches = topK(labelVectors_, v, 3);
    const float threshold = qMax(.23f, matches.first().second - .045f);
    for (const auto &match : matches)
        if (match.second >= threshold)
            names << labelNames_[match.first];
    return names.join(QStringLiteral(", "));
}
} // namespace piclocate
