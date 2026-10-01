#pragma once
#include "core/core.h"
#include "models/gpu.h"
#include "models/model_files.h"
#include <memory>
#include <onnxruntime_cxx_api.h>
namespace piclocate {
class Tokenizer {
  public:
    void load(const QString &file);
    std::vector<int64_t> encode(const QString &text) const;

  private:
    QHash<QString, int> vocabulary_, ranks_;
    QHash<int, QChar> bytes_;
    mutable QHash<QString, QStringList> cache_;
    QStringList bpe(const QString &token) const;
};
class Inference {
  public:
    explicit Inference(const QString &directory);
    std::vector<float> text(const QString &text);
    std::vector<float> image(const QImage &image);
    std::vector<float> imagePixels(std::vector<float> &pixels);
    void enableGpu();
    void releaseVision() {
        gpu_.reset();
        vision_.reset();
    }
    bool usesGpu() const { return bool(gpu_); }
    QString backend() const {
        return gpu_ ? gpu_->deviceName() + QStringLiteral(" · GPU") : QStringLiteral("CPU");
    }
    QString embeddingModel() const { return gpu_ ? GpuModelVersion : ModelVersion; }
    QString gpuFailure() const { return gpuFailure_; }
    std::vector<std::vector<float>> imageBatch(std::vector<std::vector<float>> &pixels);
    QString labels(const std::vector<float> &embedding);
    static std::vector<float> preprocess(const QImage &image);

  private:
    Ort::Env env_{ORT_LOGGING_LEVEL_ERROR, "PicLocate"};
    Ort::SessionOptions options_;
    std::unique_ptr<Ort::Session> text_, vision_;
    Tokenizer tokenizer_;
    QStringList labelNames_;
    std::vector<float> labelVectors_;
    QHash<QString, std::vector<float>> textCache_;
    QString directory_;
    QString gpuFailure_;
    std::unique_ptr<GpuVision> gpu_;
    void loadVision();
    std::vector<float> extract(Ort::Value &output);
};
} // namespace piclocate
