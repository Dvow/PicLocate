#include "models/gpu.h"
#include <cmath>
#ifdef Q_OS_WIN
#include <d3d12.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>
#endif

namespace piclocate {
void GpuVision::check(OrtStatus *status) const {
    if (!status)
        return;
    const std::string message = api_->GetErrorMessage(status);
    api_->ReleaseStatus(status);
    throw std::runtime_error(message);
}
void GpuVision::release() {
    if (!api_)
        return;
    if (session_)
        api_->ReleaseSession(session_);
    if (memory_)
        api_->ReleaseMemoryInfo(memory_);
    if (env_)
        api_->ReleaseEnv(env_);
    session_ = nullptr;
    memory_ = nullptr;
    env_ = nullptr;
}
GpuVision::~GpuVision() {
    release();
}
GpuVision::GpuVision(const QString &model) {
#ifdef Q_OS_WIN
    const auto directory = QCoreApplication::applicationDirPath() + QStringLiteral("/gpu/");
    directml_.setFileName(directory + QStringLiteral("DirectML.dll"));
    runtime_.setFileName(directory + QStringLiteral("onnxruntime-dml.dll"));
    if (!directml_.load() || !runtime_.load())
        throw std::runtime_error("The optional GPU runtime is not installed");
    using GetApiBase = const OrtApiBase *(ORT_API_CALL *)();
    using AppendDml = OrtStatus *(ORT_API_CALL *)(OrtSessionOptions *, int);
    const auto getApi = reinterpret_cast<GetApiBase>(runtime_.resolve("OrtGetApiBase"));
    const auto append = reinterpret_cast<AppendDml>(
        runtime_.resolve("OrtSessionOptionsAppendExecutionProvider_DML"));
    if (!getApi || !append || !(api_ = getApi()->GetApi(24)))
        throw std::runtime_error("The GPU runtime has an incompatible API");
    // API 24 is pinned. Only functions present in that API are accessed here;
    // no handles or session options are passed to the CPU runtime's C++ wrapper.
    struct Candidate {
        int index;
        SIZE_T memory;
        QString name;
    };
    std::vector<Candidate> candidates;
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
        for (UINT i = 0;; ++i) {
            Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
            if (FAILED(factory->EnumAdapters1(i, &adapter)))
                break;
            DXGI_ADAPTER_DESC1 description{};
            if (!adapter || FAILED(adapter->GetDesc1(&description)) ||
                (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
                continue;
            if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                         __uuidof(ID3D12Device), nullptr)))
                continue;
            candidates.push_back({int(i), description.DedicatedVideoMemory,
                                  QString::fromWCharArray(description.Description)});
        }
    }
    if (candidates.empty())
        throw std::runtime_error("No DirectX 12 hardware GPU is available; using CPU");
    std::stable_sort(candidates.begin(), candidates.end(),
                     [](const auto &a, const auto &b) { return a.memory > b.memory; });
    OrtSessionOptions *options = nullptr;
    try {
        check(api_->CreateEnv(ORT_LOGGING_LEVEL_ERROR, "PicLocate GPU", &env_));
        check(api_->DisableTelemetryEvents(env_));
        std::string lastFailure;
        for (const auto &candidate : candidates) {
            const int preferred = candidate.memory >= SIZE_T(4) * 1024 * 1024 * 1024   ? 16
                                  : candidate.memory >= SIZE_T(2) * 1024 * 1024 * 1024 ? 4
                                                                                       : 1;
            for (const int size : {16, 4, 1}) {
                if (size > preferred)
                    continue;
                try {
                    check(api_->CreateSessionOptions(&options));
                    check(api_->SetIntraOpNumThreads(options, 1));
                    check(api_->SetInterOpNumThreads(options, 1));
                    check(api_->DisableMemPattern(options));
                    check(api_->SetSessionExecutionMode(options, ORT_SEQUENTIAL));
                    check(api_->SetSessionGraphOptimizationLevel(options, ORT_ENABLE_ALL));
                    check(api_->AddFreeDimensionOverrideByName(options, "batch_size", size));
                    check(api_->AddFreeDimensionOverrideByName(options, "num_channels", 3));
                    check(api_->AddFreeDimensionOverrideByName(options, "height", 224));
                    check(api_->AddFreeDimensionOverrideByName(options, "width", 224));
                    check(append(options, candidate.index));
                    check(api_->CreateSession(env_, model.toStdWString().c_str(), options,
                                              &session_));
                    api_->ReleaseSessionOptions(options);
                    options = nullptr;
                    check(
                        api_->CreateCpuMemoryInfo(OrtArenaAllocator, OrtMemTypeDefault, &memory_));
                    device_ = candidate.name;
                    batchSize_ = size;
                    return;
                } catch (const std::exception &e) {
                    lastFailure = e.what();
                    if (options)
                        api_->ReleaseSessionOptions(options);
                    if (session_)
                        api_->ReleaseSession(session_);
                    if (memory_)
                        api_->ReleaseMemoryInfo(memory_);
                    options = nullptr;
                    session_ = nullptr;
                    memory_ = nullptr;
                }
            }
        }
        throw std::runtime_error(lastFailure.empty() ? "No compatible GPU; using CPU"
                                                     : lastFailure);
    } catch (...) {
        if (options)
            api_->ReleaseSessionOptions(options);
        release();
        throw;
    }
#else
    Q_UNUSED(model);
    throw std::runtime_error("GPU indexing currently requires Windows");
#endif
}
std::vector<std::vector<float>> GpuVision::images(const std::vector<std::vector<float>> &pixels) {
    if (pixels.empty() || pixels.size() > BatchSize)
        throw std::runtime_error("Invalid GPU batch size");
    std::vector<std::vector<float>> result;
    result.reserve(pixels.size());
    for (size_t begin = 0; begin < pixels.size(); begin += size_t(batchSize_)) {
        auto part = chunk(pixels, begin, std::min(size_t(batchSize_), pixels.size() - begin));
        for (auto &embedding : part)
            result.push_back(std::move(embedding));
    }
    return result;
}
std::vector<std::vector<float>> GpuVision::chunk(const std::vector<std::vector<float>> &pixels,
                                                 size_t begin, size_t count) {
    constexpr size_t imageSize = 3 * 224 * 224;
    // A single fixed shape lets DirectML compile once. Pad the final partial
    // batch; inference is independent per row and padded rows are discarded.
    std::vector<float> batch(size_t(batchSize_) * imageSize);
    for (size_t i = 0; i < count; ++i) {
        if (pixels[begin + i].size() != imageSize)
            throw std::runtime_error("Invalid image tensor dimensions");
        std::copy(pixels[begin + i].begin(), pixels[begin + i].end(),
                  batch.begin() + i * imageSize);
    }
    const int64_t shape[]{batchSize_, 3, 224, 224};
    OrtValue *input = nullptr, *output = nullptr;
    try {
        check(api_->CreateTensorWithDataAsOrtValue(memory_, batch.data(),
                                                   batch.size() * sizeof(float), shape, 4,
                                                   ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &input));
        const char *names[]{"pixel_values"}, *outputs[]{"image_embeds"};
        const OrtValue *inputs[]{input};
        check(api_->Run(session_, nullptr, names, inputs, 1, outputs, 1, &output));
        OrtTensorTypeAndShapeInfo *info = nullptr;
        check(api_->GetTensorTypeAndShape(output, &info));
        size_t elements = 0;
        const auto status = api_->GetTensorShapeElementCount(info, &elements);
        api_->ReleaseTensorTypeAndShapeInfo(info);
        check(status);
        if (elements != size_t(batchSize_) * Dimensions)
            throw std::runtime_error("Unexpected GPU output shape");
        float *data = nullptr;
        check(api_->GetTensorMutableData(output, reinterpret_cast<void **>(&data)));
        std::vector<std::vector<float>> result;
        for (size_t i = 0; i < count; ++i) {
            std::vector<float> vector(data + i * Dimensions, data + (i + 1) * Dimensions);
            normalize(vector);
            result.push_back(std::move(vector));
        }
        api_->ReleaseValue(input);
        api_->ReleaseValue(output);
        return result;
    } catch (...) {
        if (input)
            api_->ReleaseValue(input);
        if (output)
            api_->ReleaseValue(output);
        throw;
    }
}
} // namespace piclocate
