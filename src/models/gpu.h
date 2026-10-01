#pragma once
#include "core/core.h"
#include <onnxruntime_c_api.h>

namespace piclocate {
// A separate, pinned runtime keeps DirectML isolated from the CPU/text runtime.
// All opaque handles stay with the API that created them.
class GpuVision {
  public:
    static constexpr int BatchSize = 16;
    explicit GpuVision(const QString &model);
    ~GpuVision();
    GpuVision(const GpuVision &) = delete;
    std::vector<std::vector<float>> images(const std::vector<std::vector<float>> &pixels);
    QString deviceName() const { return device_; }

  private:
    QLibrary directml_, runtime_;
    const OrtApi *api_ = nullptr;
    OrtEnv *env_ = nullptr;
    OrtSession *session_ = nullptr;
    OrtMemoryInfo *memory_ = nullptr;
    QString device_;
    int batchSize_ = 1;
    std::vector<std::vector<float>> chunk(const std::vector<std::vector<float>> &pixels,
                                          size_t begin, size_t count);
    void check(OrtStatus *status) const;
    void release();
};
} // namespace piclocate
