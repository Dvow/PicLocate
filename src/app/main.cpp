#include "app/headless.h"
#include "core/core.h"
#include "library/database.h"
#include "ui/window.h"
#include <iostream>
using namespace piclocate;
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("PicLocate"));
    QCoreApplication::setApplicationName(QStringLiteral("PicLocate"));
    QCoreApplication::setApplicationVersion(QString::fromLatin1(PICLOCATE_VERSION));
    QImageReader::setAllocationLimit(512);
    qRegisterMetaType<Photo>();
    qRegisterMetaType<Photos>();
    qRegisterMetaType<SearchRequest>();
    qRegisterMetaType<SearchResult>();
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("PicLocate — private, native image search"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOptions(
        {{{QStringLiteral("data-dir")},
          QStringLiteral("Use a separate local library directory"),
          QStringLiteral("directory")},
         {{QStringLiteral("index")},
          QStringLiteral("Index a folder; repeat for multiple folders"),
          QStringLiteral("folder")},
         {{QStringLiteral("query")}, QStringLiteral("Search query"), QStringLiteral("text")},
         {{QStringLiteral("no-ocr")}, QStringLiteral("Disable OCR for this headless scan")},
         {{QStringLiteral("cpu")},
          QStringLiteral("Use CPU indexing instead of automatic GPU acceleration")},
         {{QStringLiteral("appearance-index")},
          QStringLiteral("Build appearance descriptors for existing images without AI inference")},
         {{QStringLiteral("similar")},
          QStringLiteral("Search using an indexed image ID"),
          QStringLiteral("id")},
         {{QStringLiteral("appearance")}, QStringLiteral("Use color and shape for --similar")},
         {{QStringLiteral("reference")},
          QStringLiteral("Search by a local reference image"),
          QStringLiteral("file")},
         {{QStringLiteral("shape-match")},
          QStringLiteral("Match structure with less color influence")},
         {{QStringLiteral("color-match")}, QStringLiteral("Match palette and color layout")},
         {{QStringLiteral("text-only")},
          QStringLiteral("Search filenames, labels, OCR and notes without AI")},
         {{QStringLiteral("headless")},
          QStringLiteral("Run indexing/search without opening a window")},
         {{QStringLiteral("report")},
          QStringLiteral("Write headless results as JSON"),
          QStringLiteral("file")},
         {{QStringLiteral("benchmark")},
          QStringLiteral("Measure exact top-100 search over 40,000 synthetic 512D vectors")},
         {{QStringLiteral("diagnostics")},
          QStringLiteral("Print platform and CPU backend information without opening a library")},
         {{QStringLiteral("screenshot")},
          QStringLiteral("Save a window preview after loading"),
          QStringLiteral("file")},
         {{QStringLiteral("exit-after")},
          QStringLiteral("Exit UI after this many milliseconds (smoke testing)"),
          QStringLiteral("milliseconds")}});
    parser.process(app);
    if (parser.isSet(QStringLiteral("diagnostics"))) {
        const QJsonObject diagnostics{
            {QStringLiteral("application"), QCoreApplication::applicationName()},
            {QStringLiteral("version"), QCoreApplication::applicationVersion()},
            {QStringLiteral("os"), QSysInfo::prettyProductName()},
            {QStringLiteral("build_architecture"), QSysInfo::buildCpuArchitecture()},
            {QStringLiteral("host_architecture"), QSysInfo::currentCpuArchitecture()},
            {QStringLiteral("logical_cpus"), QThread::idealThreadCount()},
            {QStringLiteral("vector_backend"), vectorBackend()},
            {QStringLiteral("qt"), QString::fromLatin1(qVersion())},
            {QStringLiteral("default_library"), defaultLibraryRoot()}};
        std::cout << QJsonDocument(diagnostics).toJson().constData() << std::endl;
        return 0;
    }
    Paths paths{parser.isSet(QStringLiteral("data-dir"))
                    ? QFileInfo(parser.value(QStringLiteral("data-dir"))).absoluteFilePath()
                    : defaultLibraryRoot()};
    try {
        paths.create();
        QLockFile libraryLock(paths.root + QStringLiteral("/piclocate.lock"));
        libraryLock.setStaleLockTime(0);
        if (!libraryLock.tryLock())
            throw std::runtime_error(
                "This library is already open in another PicLocate process. Close "
                "that window or choose a different --data-dir.");
        Database db(paths);
        db.initialize();
        if (parser.isSet(QStringLiteral("headless")) || parser.isSet(QStringLiteral("benchmark")))
            return runHeadless(app, parser, paths, db);
        Window window(paths);
        window.show();
        if (parser.isSet(QStringLiteral("reference")))
            window.searchImage(parser.value(QStringLiteral("reference")));
        if (parser.isSet(QStringLiteral("query")))
            window.setQuery(parser.value(QStringLiteral("query")));
        for (const auto &folder : parser.values(QStringLiteral("index")))
            window.addFolder(folder);
        if (parser.isSet(QStringLiteral("screenshot"))) {
            QTimer::singleShot(2500, &window, [&] {
                const auto warmup = window.grab();
                Q_UNUSED(warmup);
            });
            QTimer::singleShot(4500, &window, [&] {
                window.grab().save(parser.value(QStringLiteral("screenshot")));
            });
        }
        if (parser.isSet(QStringLiteral("exit-after")))
            QTimer::singleShot(parser.value(QStringLiteral("exit-after")).toInt(), &window,
                               &QWidget::close);
        return app.exec();
    } catch (const std::exception &e) {
        const auto message = QString::fromUtf8(e.what());
        if (parser.isSet(QStringLiteral("headless")) || parser.isSet(QStringLiteral("benchmark"))) {
            std::cerr << e.what() << std::endl;
            return 1;
        }
        QMessageBox::critical(nullptr, QStringLiteral("PicLocate could not start"), message);
        return 1;
    }
}
