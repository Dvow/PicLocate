#pragma once
#include "core/core.h"
#include <QTemporaryDir>

namespace piclocate::test {
class Directory : public QTemporaryDir {
  public:
    // macOS's temporary root uses /var, which resolves to /private/var.
    // Folder records are canonical, so fixtures must use the same spelling.
    Directory()
        : QTemporaryDir(normalizedPath(QDir::tempPath()) +
                        QStringLiteral("/piclocate-test-XXXXXX")) {}
};
} // namespace piclocate::test
