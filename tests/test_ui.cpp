#include "core/core.h"
#include "library/database.h"
#include "models/appearance.h"
#include "ui/viewer.h"
#include "ui/window.h"
#include <QScopeGuard>
#include <QtTest>
using namespace piclocate;

class UiTests : public QObject {
    Q_OBJECT
  private:
    static void capture(QWidget &widget, const QString &name) {
        const auto directory = qEnvironmentVariable("PICLOCATE_UI_PREVIEW_DIR");
        if (directory.isEmpty())
            return;
        QTest::qWait(50); // Let pending layout/thumbnail updates settle before visual review.
        QVERIFY(QDir().mkpath(directory));
        QVERIFY(widget.grab().save(directory + u'/' + name + QStringLiteral(".png")));
    }
    static QPushButton *findButton(Window &window, const QString &text) {
        for (auto *button : window.findChildren<QPushButton *>())
            if (button->text().trimmed() == text)
                return button;
        return nullptr;
    }
  private slots:
    void updateSettings() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        Window window(paths, {QStringLiteral("example/piclocate"), QStringLiteral("windows-x64"),
                              QStringLiteral("1.7.1")});
        window.show();
        auto *settings = window.findChild<QPushButton *>(QStringLiteral("SettingsButton"));
        QVERIFY(settings);
        for (const auto save : {false, true}) {
            bool visited = false;
            QTimer::singleShot(100, &window, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                auto *automatic = dialog->findChild<QCheckBox *>(QStringLiteral("AutoUpdates"));
                auto *check = dialog->findChild<QPushButton *>(QStringLiteral("CheckUpdates"));
                QVERIFY(automatic && check);
                QVERIFY(automatic->isChecked());
                QVERIFY(check->isEnabled());
                QCOMPARE(check->text(), QStringLiteral("Check for updates"));
                automatic->setChecked(false);
                capture(*dialog, QStringLiteral("settings-updates"));
                visited = true;
                auto *buttons = dialog->findChild<QDialogButtonBox *>();
                QVERIFY(buttons);
                QTest::mouseClick(
                    buttons->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel),
                    Qt::LeftButton);
            });
            QTest::mouseClick(settings, Qt::LeftButton);
            QVERIFY(visited);
            QSettings saved(directory.filePath(QStringLiteral("settings.ini")),
                            QSettings::IniFormat);
            QCOMPARE(saved.value(QStringLiteral("updates/automatic"), true).toBool(), !save);
        }
        window.close();
    }
    void compactLayoutAndFilterState() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        std::vector<float> embedding(Dimensions, 0.f);
        embedding[0] = 1;
        QString reference;
        for (int i = 0; i < 3; ++i) {
            QImage image(80, 80, QImage::Format_RGB32);
            image.fill(i == 0 ? Qt::blue : Qt::red);
            Photo photo;
            photo.path = directory.path() + QStringLiteral("/asset-%1.png").arg(i);
            photo.name = QFileInfo(photo.path).fileName();
            photo.folder = directory.path();
            photo.width = photo.height = 80;
            QVERIFY(image.save(photo.path));
            QByteArray encoded;
            QBuffer buffer(&encoded);
            buffer.open(QIODevice::WriteOnly);
            QVERIFY(image.save(&buffer, "PNG"));
            photo.thumb = database.saveThumbnail(encoded);
            database.save(photo, embedding);
            if (i == 0)
                database.setFavorite(database.photos().first().id, true);
            database.saveAppearance(photo, appearanceDescriptor(image));
            if (i == 0)
                reference = photo.path;
        }
        Window window(paths);
        window.resize(1060, 720);
        window.show();
        auto *gallery = window.findChild<QListView *>(QStringLiteral("Gallery"));
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("SearchBox"));
        auto *filters = window.findChild<QPushButton *>(QStringLiteral("ToggleFilters"));
        auto *panel = window.findChild<QWidget *>(QStringLiteral("FilterPanel"));
        auto *reset = window.findChild<QPushButton *>(QStringLiteral("ResetFilters"));
        auto *shape = window.findChild<QComboBox *>(QStringLiteral("ShapeFilter"));
        auto *format = window.findChild<QComboBox *>(QStringLiteral("FormatFilter"));
        QVERIFY(gallery && search && filters && panel && reset && shape && format);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        QVERIFY(!panel->isVisible());
        QVERIFY(search->width() >= 300);
        QVERIFY(gallery->viewport()->height() >= 350);
        QVERIFY(!QPixmap(QStringLiteral(":/resources/chevron.svg")).isNull());
        QVERIFY(!QPixmap(QStringLiteral(":/resources/check.svg")).isNull());
        auto *root = window.centralWidget();
        for (const auto *control :
             {static_cast<QWidget *>(search), static_cast<QWidget *>(filters),
              static_cast<QWidget *>(window.findChild<QLabel *>(QStringLiteral("Brand")))}) {
            QVERIFY(control);
            QVERIFY(root->rect().contains(QRect(control->mapTo(root, QPoint()), control->size())));
        }
        capture(window, QStringLiteral("compact"));
        QTest::mouseClick(filters, Qt::LeftButton);
        QVERIFY(panel->isVisible());
        shape->setCurrentIndex(3);
        format->setCurrentIndex(1);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        QVERIFY(reset->isVisible());
        QCOMPARE(filters->text(), QStringLiteral("Filters (2)"));
        QTest::mouseClick(filters, Qt::LeftButton);
        QVERIFY(!panel->isVisible());
        QCOMPARE(shape->currentIndex(), 3);
        QTest::mouseClick(filters, Qt::LeftButton);
        auto *favorites = findButton(window, QStringLiteral("Favorites"));
        QVERIFY(favorites);
        QTest::mouseClick(favorites, Qt::LeftButton);
        QTRY_COMPARE(gallery->model()->rowCount(), 1);
        window.searchImage(reference);
        QTRY_COMPARE(gallery->model()->rowCount(), 1);
        auto *referencePanel = window.findChild<QWidget *>(QStringLiteral("ReferencePanel"));
        auto *sort = window.findChild<QComboBox *>(QStringLiteral("SortOrder"));
        sort->setCurrentIndex(1);
        QTest::mouseClick(reset, Qt::LeftButton);
        QTRY_COMPARE(gallery->model()->rowCount(), 1);
        QVERIFY(favorites->isChecked());
        QVERIFY(referencePanel->isVisible());
        QCOMPARE(sort->currentIndex(), 1);
        QCOMPARE(shape->currentIndex(), 0);
        QCOMPARE(format->currentIndex(), 0);
        QVERIFY(!reset->isVisible());
        auto *clear = findButton(window, QStringLiteral("Clear"));
        QTest::mouseClick(clear, Qt::LeftButton);
        QTest::mouseClick(findButton(window, QStringLiteral("All images")), Qt::LeftButton);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        gallery->setCurrentIndex(gallery->model()->index(0, 0));
        QTRY_VERIFY(!static_cast<GalleryModel *>(gallery->model())->thumbnail(0).isNull());
        capture(window, QStringLiteral("details"));
        window.findChild<QComboBox *>(QStringLiteral("SearchMode"))->setCurrentIndex(2);
        window.setQuery(QStringLiteral("missing-asset"));
        QTRY_COMPARE(gallery->model()->rowCount(), 0);
        capture(window, QStringLiteral("no-results"));
        window.setQuery({});
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        auto *settings = window.findChild<QPushButton *>(QStringLiteral("SettingsButton"));
        QVERIFY(settings);
        for (const bool save : {false, true}) {
            bool visited = false;
            QTimer::singleShot(100, &window, [&] {
                auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const auto close = qScopeGuard([dialog] { dialog->reject(); });
                auto *autoScan = dialog->findChild<QCheckBox *>(QStringLiteral("AutoScan"));
                auto *maintenance =
                    dialog->findChild<QPushButton *>(QStringLiteral("ToggleMaintenance"));
                auto *maintenancePanel =
                    dialog->findChild<QWidget *>(QStringLiteral("MaintenancePanel"));
                auto *actions = dialog->findChild<QDialogButtonBox *>();
                QVERIFY(autoScan && maintenance && maintenancePanel && actions);
                QVERIFY(!maintenancePanel->isVisible());
                capture(*dialog, QStringLiteral("settings"));
                QTest::mouseClick(maintenance, Qt::LeftButton);
                QVERIFY(maintenancePanel->isVisible());
                capture(*dialog, QStringLiteral("maintenance"));
                autoScan->setChecked(true);
                visited = true;
                QTest::mouseClick(
                    actions->button(save ? QDialogButtonBox::Save : QDialogButtonBox::Cancel),
                    Qt::LeftButton);
            });
            QTest::mouseClick(settings, Qt::LeftButton);
            QVERIFY(visited);
            QSettings preferences(paths.root + QStringLiteral("/settings.ini"),
                                  QSettings::IniFormat);
            QCOMPARE(preferences.value(QStringLiteral("library/autoScan"), false).toBool(), save);
        }
        window.close();
    }
    void largeLibraryScrollingBenchmark() {
        const auto directory = qEnvironmentVariable("PICLOCATE_SCROLL_BENCHMARK");
        if (directory.isEmpty())
            QSKIP("Optional full-library scrolling benchmark on an isolated copy");
        QVERIFY2(QFileInfo::exists(directory + QStringLiteral("/benchmark-copy.marker")),
                 "Use an isolated benchmark copy, never the live library");
        Paths paths{directory};
        Database database(paths);
        const int total = database.count();
        QVERIFY(total >= 5000);
        Window window(paths);
        window.show();
        auto *gallery = window.findChild<QListView *>(QStringLiteral("Gallery"));
        auto *model = static_cast<GalleryModel *>(gallery->model());
        auto *bar = gallery->verticalScrollBar();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 300, 15000);
        QSignalSpy resets(model, &QAbstractItemModel::modelReset);
        QSignalSpy pages(model, &QAbstractItemModel::rowsInserted);
        QElapsedTimer elapsed;
        elapsed.start();
        qint64 previousTick = 0;
        QList<qint64> intervals;
        QTimer heartbeat;
        heartbeat.setInterval(10);
        connect(&heartbeat, &QTimer::timeout, this, [&] {
            const auto now = elapsed.elapsed();
            intervals.append(now - previousTick);
            previousTick = now;
        });
        heartbeat.start();
        while (model->rowCount() < total) {
            const int before = model->rowCount();
            QTRY_VERIFY_WITH_TIMEOUT((bar->setValue(bar->maximum()), model->rowCount() > before),
                                     10000);
        }
        heartbeat.stop();
        const auto wallMs = elapsed.elapsed();
        QCOMPARE(resets.size(), 0);
        QSet<qint64> ids;
        for (const auto &photo : model->photos())
            ids.insert(photo.id);
        QCOMPARE(ids.size(), total);
        const auto expected = database.photos();
        for (int i = 0; i < total; ++i)
            QCOMPARE(model->photo(i).id, expected[i].id);
        QVERIFY(!intervals.isEmpty());
        std::sort(intervals.begin(), intervals.end());
        const QJsonObject report{
            {QStringLiteral("images"), total},
            {QStringLiteral("appended_pages"), pages.size()},
            {QStringLiteral("model_resets_while_scrolling"), resets.size()},
            {QStringLiteral("scroll_wall_ms"), wallMs},
            {QStringLiteral("heartbeat_interval_ms"), 10},
            {QStringLiteral("p95_event_loop_gap_ms"), intervals[(intervals.size() - 1) * 95 / 100]},
            {QStringLiteral("max_event_loop_gap_ms"), intervals.last()}};
        QFile output(directory + QStringLiteral("/scroll-report.json"));
        QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(report).toJson());
        qInfo().noquote() << QJsonDocument(report).toJson(QJsonDocument::Compact);
        window.close();
    }
    void libraryInteraction_data() {
        QTest::addColumn<QString>("modelVersion");
        QTest::newRow("current") << ModelVersion;
        QTest::newRow("compatible-upgrade") << CompatibleLegacyModelVersion;
        QTest::newRow("previous-upgrade") << CompatiblePreviousModelVersion;
        QTest::newRow("incompatible-browsing") << QStringLiteral("another-model");
    }
    void libraryInteraction() {
        QFETCH(QString, modelVersion);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        for (int i = 0; i < 3; ++i) {
            Photo photo;
            photo.folder = normalizedPath(directory.path());
            photo.path = directory.path() + QStringLiteral("/fixture-%1.png").arg(i);
            photo.name = QFileInfo(photo.path).fileName();
            photo.thumb = photo.path;
            photo.width = i == 0 ? 300 : 600;
            photo.height = 400;
            QImage image(photo.width, photo.height, QImage::Format_RGB32);
            image.fill(QColor::fromHsv(i * 85, 130, 200));
            QVERIFY(image.save(photo.path));
            std::vector<float> vector(Dimensions, 0);
            vector[size_t(i)] = 1;
            database.save(photo, vector);
        }
        const auto connection = QUuid::createUuid().toString();
        {
            auto fixture = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            fixture.setDatabaseName(paths.database());
            QVERIFY(fixture.open());
            QSqlQuery query(fixture);
            query.prepare(QStringLiteral("UPDATE photos SET model=?"));
            query.addBindValue(modelVersion);
            QVERIFY(query.exec());
        }
        QSqlDatabase::removeDatabase(connection);
        Window window(paths);
        window.show();
        auto *gallery = window.findChild<QListView *>(QStringLiteral("Gallery"));
        QVERIFY(gallery);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        // Select a real gallery card; the details panel must populate.
        gallery->setCurrentIndex(gallery->model()->index(0, 0));
        QCoreApplication::processEvents();
        auto *favorite = findButton(window, QStringLiteral("Add to favorites"));
        QVERIFY(favorite && favorite->isVisible());
        QTest::mouseClick(favorite, Qt::LeftButton);
        QCOMPARE(database.photos()[0].favorite, true);
        auto *notes = window.findChild<QPlainTextEdit *>();
        QVERIFY(notes);
        notes->setPlainText(QStringLiteral("Blue Mountain invoice 4172"));
        QVERIFY(!findButton(window, QStringLiteral("Save description")));
        QTRY_VERIFY(database.lexical(QStringLiteral("4172")).contains(database.photos()[0].id));
        auto *favorites = findButton(window, QStringLiteral("Favorites"));
        QVERIFY(favorites);
        QTest::mouseClick(favorites, Qt::LeftButton);
        QTRY_COMPARE(gallery->model()->rowCount(), 1);
        auto *all = findButton(window, QStringLiteral("All images"));
        QVERIFY(all);
        QTest::mouseClick(all, Qt::LeftButton);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        auto *shape = window.findChild<QComboBox *>(QStringLiteral("ShapeFilter"));
        QVERIFY(shape);
        shape->setCurrentIndex(2);
        QTRY_COMPARE(gallery->model()->rowCount(), 1);
        auto *search = window.findChild<QLineEdit *>(QStringLiteral("SearchBox"));
        QVERIFY(search);
        search->setFocus();
        shape->setCurrentIndex(0);
        auto *mode = window.findChild<QComboBox *>(QStringLiteral("SearchMode"));
        QVERIFY(mode);
        mode->setCurrentIndex(2);
        search->setText(QStringLiteral("4172"));
        QTRY_COMPARE(gallery->model()->rowCount(), 1);
        QTRY_VERIFY(window.findChild<QLabel *>(QStringLiteral("SearchStatus"))
                        ->text()
                        .startsWith(QStringLiteral("Text matches")));
        QCOMPARE(static_cast<GalleryModel *>(gallery->model())->photos().first().notes,
                 QStringLiteral("Blue Mountain invoice 4172"));
        search->clear();
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        gallery->setCurrentIndex(gallery->model()->index(0, 0));
        const auto firstId = static_cast<GalleryModel *>(gallery->model())->photo(0).id;
        notes->setPlainText(QStringLiteral("Autosaved before selection changes"));
        gallery->setCurrentIndex(gallery->model()->index(1, 0));
        QCOMPARE(database.photo(firstId).notes,
                 QStringLiteral("Autosaved before selection changes"));
        notes->setPlainText(QStringLiteral("Autosaved on close"));
        const auto secondId = static_cast<GalleryModel *>(gallery->model())->photo(1).id;
        QVERIFY(window.styleSheet().contains(QStringLiteral("#0c131e")));
        window.close();
        QCOMPARE(database.photo(secondId).notes, QStringLiteral("Autosaved on close"));
    }
    void appearanceSearchControls() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        std::vector<float> embedding(Dimensions, 0.f);
        embedding[0] = 1;
        for (int i = 0; i < 3; ++i) {
            QImage image(80, 80, QImage::Format_RGB32);
            image.fill(i == 0 ? Qt::blue : Qt::red);
            Photo photo;
            photo.folder = directory.path();
            photo.path = directory.path() + QStringLiteral("/%1.png").arg(i);
            photo.name = QFileInfo(photo.path).fileName();
            QByteArray encoded;
            QBuffer buffer(&encoded);
            buffer.open(QIODevice::WriteOnly);
            QVERIFY(image.save(&buffer, "PNG"));
            photo.thumb = database.saveThumbnail(encoded);
            QVERIFY(image.save(photo.path));
            database.save(photo, embedding);
            database.saveAppearance(photo, appearanceDescriptor(image));
        }
        Window window(paths);
        window.show();
        auto *gallery = window.findChild<QListView *>(QStringLiteral("Gallery"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("SearchStatus"));
        auto *appearance = window.findChild<QPushButton *>(QStringLiteral("MatchAppearance"));
        QVERIFY(gallery && status && appearance);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        auto *model = static_cast<GalleryModel *>(gallery->model());
        QTRY_VERIFY(!model->thumbnail(0).isNull());
        gallery->setCurrentIndex(gallery->model()->index(0, 0));
        QTRY_VERIFY(appearance->isVisible());
        QTest::mouseClick(appearance, Qt::LeftButton);
        QTRY_VERIFY(status->text().startsWith(QStringLiteral("Appearance")));
        auto *reference = window.findChild<QWidget *>(QStringLiteral("ReferencePanel"));
        auto *similarity = window.findChild<QComboBox *>(QStringLiteral("SimilarityMode"));
        QVERIFY(reference && reference->isVisible() && similarity);
        QTRY_VERIFY(
            !window.findChild<QLabel *>(QStringLiteral("ReferencePreview"))->pixmap().isNull());
        similarity->setCurrentIndex(similarity->findData(int(SimilarityMode::Shape)));
        QTRY_VERIFY(status->text().contains(QStringLiteral("shape and detail")));
        QCOMPARE(gallery->model()->rowCount(), 2);
        QCOMPARE(static_cast<GalleryModel *>(gallery->model())->photo(0).name,
                 QStringLiteral("1.png"));
        auto *clear = findButton(window, QStringLiteral("Clear"));
        QVERIFY(clear && clear->isVisible());
        QTest::mouseClick(clear, Qt::LeftButton);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        QVERIFY(!reference->isVisible());
        QVERIFY(!clear->isVisible());
        window.searchImage(directory.path() + QStringLiteral("/2.png"));
        QTRY_COMPARE(gallery->model()->rowCount(), 2);
        QVERIFY(reference->isVisible());
        QCOMPARE(database.count(), 3);
        auto *size = window.findChild<QSlider *>(QStringLiteral("ThumbnailSize"));
        QVERIFY(size);
        size->setValue(160);
        QCOMPARE(model->cardWidth(), 160);
        QTest::mouseClick(clear, Qt::LeftButton);
        QTRY_COMPARE(gallery->model()->rowCount(), 3);
        auto *rescan = findButton(window, QStringLiteral("Rescan"));
        QVERIFY(rescan && rescan->menu());
        QCOMPARE(rescan->menu()->actions().size(), 2);
        window.close();
    }
    void paginationAndThumbnailUpdates() {
        QTemporaryDir directory;
        Paths paths{directory.path()};
        paths.create();
        Database database(paths);
        database.initialize();
        database.addFolder(directory.path());
        QImage image(80, 80, QImage::Format_RGB32);
        image.fill(Qt::green);
        const auto thumbnail = directory.path() + QStringLiteral("/thumbnail.png");
        QVERIFY(image.save(thumbnail));
        std::vector<float> vector(Dimensions, 0.f);
        vector[0] = 1;
        database.begin();
        for (int i = 0; i < 650; ++i) {
            Photo photo;
            photo.folder = normalizedPath(directory.path());
            photo.path = directory.path() + QStringLiteral("/%1.png").arg(i);
            photo.name = QString::number(i);
            photo.thumb = thumbnail;
            database.save(photo, vector);
        }
        database.commit();
        Window window(paths);
        window.show();
        auto *gallery = window.findChild<QListView *>(QStringLiteral("Gallery"));
        QVERIFY(!window.findChild<QPushButton *>(QStringLiteral("LoadMore")));
        QTRY_COMPARE(gallery->model()->rowCount(), 300);
        QTest::qWait(150);
        QCOMPARE(gallery->model()->rowCount(), 300); // Do not eagerly drain the library.
        gallery->setCurrentIndex(gallery->model()->index(3, 0));
        const auto selected = static_cast<GalleryModel *>(gallery->model())->photo(3).id;
        // Opening the inspector changes the viewport and schedules Qt's grid layout.
        QTest::qWait(150);
        QSignalSpy resets(gallery->model(), &QAbstractItemModel::modelReset);
        QSignalSpy insertions(gallery->model(), &QAbstractItemModel::rowsInserted);
        auto *bar = gallery->verticalScrollBar();
        QTRY_VERIFY(bar->maximum() > gallery->viewport()->height() * 3);
        const int position = bar->maximum() - gallery->viewport()->height();
        bar->setValue(position); // Fetch before the end, without any button press.
        QTimer continuousScroll;
        bool tick = false;
        connect(&continuousScroll, &QTimer::timeout, this, [&] {
            tick = !tick;
            bar->setValue(position - int(tick));
        });
        continuousScroll.start(5);
        QTRY_COMPARE(gallery->model()->rowCount(), 600);
        continuousScroll.stop();
        QCOMPARE(resets.size(), 0);
        QCOMPARE(insertions.size(), 1);
        QVERIFY(qAbs(bar->value() - position) <= 1);
        QCOMPARE(
            static_cast<GalleryModel *>(gallery->model())->photo(gallery->currentIndex().row()).id,
            selected);
        QVERIFY(window.findChild<QWidget *>(QStringLiteral("Inspector"))->isVisible());
        QTest::qWait(150);
        QCOMPARE(gallery->model()->rowCount(), 600);
        bar->setValue(bar->maximum());
        bar->setValue(bar->maximum() - 1);
        bar->setValue(bar->maximum());
        QTRY_COMPARE(gallery->model()->rowCount(), 650);
        QCOMPARE(insertions.size(), 2);
        QSet<qint64> ids;
        for (const auto &photo : static_cast<GalleryModel *>(gallery->model())->photos())
            ids.insert(photo.id);
        QCOMPARE(ids.size(), 650);
        bar->setValue(bar->maximum());
        QTest::qWait(150);
        QCOMPARE(insertions.size(), 2); // No retry loop at the end.
        auto *mode = window.findChild<QComboBox *>(QStringLiteral("SearchMode"));
        mode->setCurrentIndex(2);
        window.setQuery(QStringLiteral("349"));
        QTRY_COMPARE(gallery->model()->rowCount(), 1);
        QCOMPARE(static_cast<GalleryModel *>(gallery->model())->photo(0).name,
                 QStringLiteral("349"));
        window.setQuery(QString());
        QTRY_COMPARE(gallery->model()->rowCount(), 300);
        QTRY_COMPARE(bar->value(), 0);
        // A new query wins over a queued near-bottom prefetch.
        bar->setValue(bar->maximum());
        window.setQuery(QStringLiteral("no-such-image"));
        QTRY_COMPARE(gallery->model()->rowCount(), 0);
        QTest::qWait(150);
        QCOMPARE(gallery->model()->rowCount(), 0);
        window.close();
        GalleryModel model;
        Photos photos;
        for (int i = 0; i < 3; ++i) {
            Photo photo;
            photo.thumb =
                i == 1 ? thumbnail : directory.path() + QStringLiteral("/missing%1.jpg").arg(i);
            photos << photo;
        }
        model.setPhotos(photos);
        QSignalSpy spy(&model, &GalleryModel::dataChanged);
        model.thumbnail(1);
        QTRY_VERIFY(!spy.isEmpty());
        QCOMPARE(qvariant_cast<QModelIndex>(spy.first()[0]).row(), 1);
        QCOMPARE(qvariant_cast<QModelIndex>(spy.first()[1]).row(), 1);
        QVERIFY(!model.thumbnail(1).isNull());
    }
    void viewerNavigationAndZoom() {
        QTemporaryDir directory;
        Photos photos;
        for (int i = 0; i < 2; ++i) {
            Photo photo;
            photo.id = i + 1;
            photo.path = directory.path() + QStringLiteral("/%1.png").arg(i);
            photo.name = QStringLiteral("image-%1").arg(i);
            QImage image(160, 120, QImage::Format_RGB32);
            image.fill(i == 0 ? Qt::green : Qt::blue);
            QVERIFY(image.save(photo.path));
            photos << photo;
        }
        ImageViewer viewer(photos, 1);
        viewer.show();
        auto *canvas = viewer.findChild<QGraphicsView *>();
        QVERIFY(canvas);
        QTRY_COMPARE(canvas->scene()->items().size(), 1);
        QCOMPARE(viewer.windowTitle(), QStringLiteral("image-0"));
        QPushButton *next = nullptr, *zoom = nullptr;
        for (auto *button : viewer.findChildren<QPushButton *>()) {
            if (button->text().startsWith(QStringLiteral("Next")))
                next = button;
            if (button->accessibleName() == QStringLiteral("Zoom in"))
                zoom = button;
        }
        QVERIFY(next && zoom);
        QTest::mouseClick(next, Qt::LeftButton);
        QTRY_COMPARE(canvas->scene()->items().size(), 1);
        QCOMPARE(viewer.windowTitle(), QStringLiteral("image-1"));
        auto *item = qgraphicsitem_cast<QGraphicsPixmapItem *>(canvas->scene()->items().first());
        QVERIFY(item);
        QCOMPARE(item->pixmap().toImage().pixelColor(0, 0), QColor(Qt::blue));
        capture(viewer, QStringLiteral("viewer"));
        const auto scale = canvas->transform().m11();
        QTest::mouseClick(zoom, Qt::LeftButton);
        QVERIFY(canvas->transform().m11() > scale);
        viewer.close();
    }
};
int main(int argc, char **argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    qRegisterMetaType<Photo>();
    qRegisterMetaType<Photos>();
    qRegisterMetaType<SearchRequest>();
    qRegisterMetaType<SearchResult>();
    UiTests test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_ui.moc"
