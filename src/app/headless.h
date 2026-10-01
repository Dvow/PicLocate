#pragma once
#include "core/types.h"
class QCommandLineParser;
class QCoreApplication;

namespace piclocate {
class Database;
int runHeadless(QCoreApplication &app, const QCommandLineParser &parser, const Paths &paths,
                Database &db);
} // namespace piclocate
