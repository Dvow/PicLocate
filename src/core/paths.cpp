#include "core/core.h"

namespace piclocate {
QString defaultLibraryRoot() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
}
QString Paths::models() const {
    const QString override = qEnvironmentVariable("PICLOCATE_MODEL_DIR");
    if (!override.isEmpty())
        return override;
    const QString bundled = QCoreApplication::applicationDirPath() + QStringLiteral("/models");
    if (QFileInfo::exists(bundled + QStringLiteral("/vision_model_quantized.onnx")))
        return bundled;
#ifdef Q_OS_MACOS
    const QString resources =
        QCoreApplication::applicationDirPath() + QStringLiteral("/../Resources/models");
    if (QFileInfo::exists(resources + QStringLiteral("/vision_model_quantized.onnx")))
        return resources;
#endif
    const QString installed =
        QCoreApplication::applicationDirPath() + QStringLiteral("/../share/PicLocate/models");
    if (QFileInfo::exists(installed + QStringLiteral("/vision_model_quantized.onnx")))
        return installed;
    return root + QStringLiteral("/models");
}
void Paths::create() const {
    QDir().mkpath(root);
    QDir().mkpath(thumbnails());
    QDir().mkpath(models());
}
} // namespace piclocate
