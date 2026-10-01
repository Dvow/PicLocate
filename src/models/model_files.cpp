#include "models/model_files.h"
#include <QFileInfo>

namespace piclocate {
ModelFile gpuModelFile() {
    return {QStringLiteral("vision_model_fp16.onnx"), QStringLiteral("onnx/vision_model_fp16.onnx"),
            QStringLiteral("35c4e0fb0aeee527dcde1693520b214a34424a786babd530f35366bad5844efd"),
            176080659};
}
QList<ModelFile> modelFiles() {
    QList<ModelFile> files{
        {QStringLiteral("text_model_quantized.onnx"),
         QStringLiteral("onnx/text_model_quantized.onnx"),
         QStringLiteral("73baab855d406190da9faa498cfedf65f15cf309f4cc7385b7b032e6d08e5c3a"),
         64504507},
        {QStringLiteral("vision_model_quantized.onnx"),
         QStringLiteral("onnx/vision_model_quantized.onnx"),
         QStringLiteral("583fd1110a514667812fee7d684952aaf82a99b959760c8d7dca7e0ab9839299"),
         89117001},
        {QStringLiteral("tokenizer.json"), QStringLiteral("tokenizer.json"),
         QStringLiteral("f7f3b7af117d467b58374797691a6438d3e6b9e9cef800dfd5dced7f697a90cd"),
         2224119}};
#ifdef Q_OS_WIN
    files.append(gpuModelFile());
#endif
    return files;
}
bool modelsPresent(const QString &dir) {
    for (const auto &f : modelFiles())
        if (f.name != gpuModelFile().name && QFileInfo(dir + u'/' + f.name).size() != f.size)
            return false;
    return true;
}
} // namespace piclocate
