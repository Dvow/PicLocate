#include "ui/window.h"
#include "core/core.h"
#include "library/database.h"
#include "library/indexer.h"
#include "library/search.h"
#include "models/model_files.h"
#include "ui/ui_helpers.h"
#include "ui/viewer.h"
#include <QDesktopServices>
#include <QtConcurrent>
namespace piclocate {
Window::Window(Paths paths, UpdateSource updates)
    : paths_(std::move(paths)),
      settings_(paths_.root + QStringLiteral("/settings.ini"), QSettings::IniFormat),
      updater_(paths_.root + QStringLiteral("/updates"), std::move(updates), nullptr) {
    paths_.create();
    db_ = std::make_unique<Database>(paths_);
    db_->initialize();
    setWindowTitle(QStringLiteral("PicLocate — Local Image Search"));
    setMinimumSize(1060, 720);
    setAcceptDrops(true);
    resize(1490, 940);
    setWindowIcon(icon(QStringLiteral("spark"), QColor(QStringLiteral("#a8c7fa"))));
    setStyleSheet(piclocate::styleSheet());
    buildUi();
    indexer_ = new Indexer(paths_);
    searcher_ = new Searcher(paths_);
    indexer_->moveToThread(&indexThread_);
    searcher_->moveToThread(&searchThread_);
    connect(&indexThread_, &QThread::finished, indexer_, &QObject::deleteLater);
    connect(&searchThread_, &QThread::finished, searcher_, &QObject::deleteLater);
    connect(searcher_, &Searcher::results, this, &Window::applyResults);
    connect(indexer_, &Indexer::stage, this, [this](const QString &s) {
        indexLabel_->setText(s);
        progress_->setRange(0, 0);
    });
    connect(indexer_, &Indexer::backendChanged, this,
            [this](const QString &name) { indexingBackend_ = name; });
    connect(indexer_, &Indexer::batchCommitted, this, [this] {
        requestSearch(true);
        refreshStats();
    });
    connect(
        indexer_, &Indexer::progress, this,
        [this](int done, int total, const QString &name, int changed, int skipped) {
            progress_->setRange(0, qMax(1, total));
            progress_->setValue(done);
            indexLabel_->setText(QStringLiteral("%1 / %2 images  ·  %3 indexed  ·  %4 unchanged")
                                     .arg(done)
                                     .arg(total)
                                     .arg(changed)
                                     .arg(skipped));
            indexLabel_->setToolTip(indexingBackend_.isEmpty() ? name
                                                               : indexingBackend_ + u'\n' + name);
            if (!indexingBackend_.isEmpty())
                indexLabel_->setText(indexLabel_->text() + QStringLiteral("  ·  ") +
                                     (indexingBackend_.endsWith(QStringLiteral(" · GPU"))
                                          ? QStringLiteral("GPU")
                                          : QStringLiteral("CPU")));
            if (changed && scanElapsed_.elapsed() > 0)
                indexLabel_->setText(
                    indexLabel_->text() +
                    QStringLiteral("  ·  %1 images/s")
                        .arg(double(changed) * 1000. / scanElapsed_.elapsed(), 0, 'f', 0));
            if (done > skipped && total > done && scanElapsed_.elapsed() > 1500) {
                const auto seconds = qint64(double(scanElapsed_.elapsed()) / 1000.0 *
                                            (total - done) / (done - skipped));
                indexLabel_->setText(indexLabel_->text() +
                                     QStringLiteral("  ·  about %1 min remaining")
                                         .arg(qMax(qint64(1), (seconds + 59) / 60)));
            }
        });
    connect(
        indexer_, &Indexer::completed, this,
        [this](int changed, int skipped, int failed, bool cancelled, const QStringList &errors) {
            indexing_ = false;
            indexPanel_->hide();
            scanButton_->setEnabled(true);
            addButton_->setEnabled(true);
            refreshStats();
            requestSearch(true);
            const QString message =
                QStringLiteral("%1 · %2 indexed · %3 unchanged%4")
                    .arg(cancelled ? QStringLiteral("Paused — rescan to resume")
                                   : QStringLiteral("Index complete"))
                    .arg(changed)
                    .arg(skipped)
                    .arg(failed ? QStringLiteral(" · %1 skipped with errors").arg(failed)
                                : QString());
            status_->setText(message);
            if (!errors.isEmpty()) {
                QFile log(paths_.root + QStringLiteral("/last-scan-errors.txt"));
                if (log.open(QIODevice::WriteOnly))
                    log.write(errors.join(u'\n').toUtf8());
                showDetailsText(QStringLiteral("Scan report"),
                                message + QStringLiteral("\n\n") + errors.join(u'\n'));
            }
        });
    indexThread_.start();
    searchThread_.start();
    debounce_.setSingleShot(true);
    debounce_.setInterval(180);
    notesTimer_.setSingleShot(true);
    notesTimer_.setInterval(650);
    connect(&notesTimer_, &QTimer::timeout, this, &Window::saveNotes);
    connect(&debounce_, &QTimer::timeout, this, [this] { requestSearch(); });
    prefetchTimer_.setSingleShot(true);
    prefetchTimer_.setInterval(35);
    connect(&prefetchTimer_, &QTimer::timeout, this, &Window::prefetchMore);
    rescanTimer_.setInterval(60000);
    connect(&rescanTimer_, &QTimer::timeout, this, [this] {
        checkForUpdates();
        if (!indexing_ && settings_.value(QStringLiteral("library/autoScan"), false).toBool())
            scan();
    });
    rescanTimer_.start();
    refreshFolders();
    refreshStats();
    requestSearch(true);
    if (settings_.contains(QStringLiteral("window/geometry")))
        restoreGeometry(settings_.value(QStringLiteral("window/geometry")).toByteArray());
    connect(&updater_, &AppUpdate::changed, this, [this] {
        if (updater_.state() == AppUpdate::State::Available)
            status_->setText(updater_.message() + QStringLiteral(" Open Settings to install."));
        if (updater_.state() == AppUpdate::State::Checking)
            settings_.setValue(QStringLiteral("updates/lastCheck"),
                               QDateTime::currentDateTimeUtc());
    });
    QTimer::singleShot(3000, this, &Window::checkForUpdates);
}
void Window::checkForUpdates() {
    if (qEnvironmentVariableIsSet("PICLOCATE_DISABLE_UPDATE_CHECK") || !updater_.enabled() ||
        !settings_.value(QStringLiteral("updates/automatic"), true).toBool())
        return;
    const auto lastCheck = settings_.value(QStringLiteral("updates/lastCheck")).toDateTime();
    if (!lastCheck.isValid() || lastCheck.secsTo(QDateTime::currentDateTimeUtc()) >= 86400)
        updater_.check();
}
Window::~Window() {
    indexer_->cancel();
    indexThread_.quit();
    searchThread_.quit();
    indexThread_.wait();
    searchThread_.wait();
}
void Window::closeEvent(QCloseEvent *e) {
    prefetchTimer_.stop();
    saveNotes();
    if (indexing_) {
        indexer_->cancel();
        status_->setText(QStringLiteral("Finishing the current image and saving progress…"));
    }
    settings_.setValue(QStringLiteral("window/geometry"), saveGeometry());
    download_.cancel();
    updater_.cancel();
    e->accept();
}
bool Window::eventFilter(QObject *watched, QEvent *event) {
    if (watched == gallery_->viewport() && event->type() == QEvent::Resize)
        schedulePrefetch();
    return QMainWindow::eventFilter(watched, event);
}
void Window::refreshFolders() {
    folders_->clear();
    for (const auto &path : db_->folders()) {
        auto *item = new QListWidgetItem(
            icon(QStringLiteral("folder")),
            QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName(), folders_);
        item->setData(Qt::UserRole, path);
        item->setToolTip(
            path + QStringLiteral("\nRight-click to disconnect. Originals are never deleted."));
    }
}
void Window::refreshStats() {
    const auto stats = db_->stats();
    addButton_->setVisible(stats.images != 0);
    scanButton_->setVisible(stats.images != 0 || indexing_);
    searchPanel_->setVisible(stats.images != 0);
    resultsToolbar_->setVisible(stats.images != 0);
    thumbnailControls_->setVisible(stats.images != 0);
    librarySummary_->setText(QStringLiteral("%1 images · %2")
                                 .arg(QLocale().toString(stats.images), fileSize(stats.bytes)));
    const bool ready = modelsPresent(paths_.models());
    modelState_->setText(ready ? QStringLiteral("Offline") : QStringLiteral("Search setup needed"));
    modelState_->setToolTip(
        ready ? QStringLiteral("Images and queries stay on this device.")
              : QStringLiteral("Open Settings to download the local search models."));
    const auto outdated = stats.outdated;
    const int missingAppearance = stats.images - stats.appearances;
    subtitle_->setText(
        missingAppearance
            ? QStringLiteral(
                  "%1 images can gain color and shape matching. Rescan → Build appearance index.")
                  .arg(QLocale().toString(missingAppearance))
        : outdated ? QStringLiteral("%1 images have an older index. Your library stays "
                                    "available; Rescan refreshes visual search.")
                         .arg(QLocale().toString(outdated))
                   : QString());
    subtitle_->setVisible(missingAppearance || outdated);
}
void Window::setNavigation(bool favorites) {
    favorites_ = favorites;
    folder_.clear();
    folders_->clearSelection();
    allButton_->setChecked(!favorites);
    favoritesButton_->setChecked(favorites);
    title_->setText(favorites ? QStringLiteral("Favorites") : QStringLiteral("All images"));
    title_->setToolTip({});
    requestSearch();
}
void Window::resetFilters() {
    orientation_->setCurrentIndex(0);
    format_->setCurrentIndex(0);
}
void Window::requestSearch(bool reload, bool more) {
    if (more && (searchPending_ || debounce_.isActive() || pagingStopped_ ||
                 galleryModel_->rowCount() >= resultTotal_ || !resultCursor_))
        return;
    debounce_.stop();
    prefetchTimer_.stop();
    if (!more) {
        resultCursor_ = 0;
        resultTotal_ = 0;
        pagingStopped_ = false;
    }
    searchPending_ = true;
    pageStatus_->setText(more ? QStringLiteral("Loading more…") : QString());
    pageStatus_->setVisible(more);
    ++generation_;
    searcher_->setLatest(generation_);
    if (reload)
        QMetaObject::invokeMethod(searcher_, &Searcher::reload, Qt::QueuedConnection);
    SearchRequest r;
    r.generation = generation_;
    r.query = searchBox_->text();
    r.folder = folder_;
    r.favorites = favorites_;
    r.similarId = similarId_;
    r.referencePath = referencePath_;
    r.similarity = similarity_;
    r.orientation = orientation_->currentIndex();
    r.mode = static_cast<SearchMode>(searchMode_->currentIndex());
    r.sort = static_cast<SortOrder>(sortOrder_->currentIndex());
    r.extension = format_->currentIndex() ? format_->currentText().toLower() : QString();
    r.limit = 300;
    r.offset = more ? galleryModel_->rowCount() : 0;
    r.cursor = more ? resultCursor_ : 0;
    QMetaObject::invokeMethod(
        searcher_, [worker = searcher_, r] { worker->search(r); }, Qt::QueuedConnection);
    if (!more && (!r.query.isEmpty() || similarId_ || !referencePath_.isEmpty()))
        status_->setText(QStringLiteral("Searching on your device…"));
}
void Window::schedulePrefetch() {
    // Throttle rather than debounce: continuous wheel/trackpad events must
    // still fetch ahead while scrolling, not wait for the user to stop.
    if (!prefetchTimer_.isActive())
        prefetchTimer_.start();
}
void Window::prefetchMore() {
    if (!gallery_->isVisible() || searchPending_ || debounce_.isActive() ||
        galleryModel_->rowCount() == 0)
        return;
    const auto *bar = gallery_->verticalScrollBar();
    const int height = gallery_->viewport()->height();
    // QListView may retain its uniform item size while a resize is pending.
    // Use actual laid-out geometry so inspector/density changes stay accurate.
    auto card = gallery_->visualRect(galleryModel_->index(0)).size();
    if (card.isEmpty())
        card = galleryCardSize(gallery_->viewport()->width(), galleryModel_->cardWidth());
    const int columns = qMax(1, gallery_->viewport()->width() / card.width());
    const int first = bar->value() / card.height() * columns;
    const int last =
        qMin(galleryModel_->rowCount(), ((bar->value() + height) / card.height() + 3) * columns);
    // Visible cards go first; then warm two upcoming rows. The existing two
    // workers, 32-request ceiling and 64 MiB cache keep decoding bounded.
    for (int row = first; row < last; ++row)
        galleryModel_->thumbnail(row);
    if (pagingStopped_ || galleryModel_->rowCount() >= resultTotal_)
        return;
    const int ahead = height * 2;
    if (bar->maximum() - bar->value() > ahead)
        return;
    // Check full row geometry too, so a pending range/size update cannot
    // accidentally request every remaining page.
    const qint64 rows = (galleryModel_->rowCount() + columns - 1) / columns;
    if (rows * card.height() - bar->value() - height > ahead)
        return;
    requestSearch(false, true);
}
void Window::applyResults(const SearchResult &r) {
    if (r.generation != generation_)
        return;
    pageStatus_->clear();
    pageStatus_->hide();
    if (r.offset > 0) {
        if (r.error || r.cursor != resultCursor_ || r.offset != galleryModel_->rowCount() ||
            r.photos.isEmpty()) {
            searchPending_ = false;
            pagingStopped_ = true; // Do not hammer a failed/exhausted continuation.
            if (r.error)
                status_->setText(r.status);
            return;
        }
        galleryModel_->appendPhotos(r.photos);
    } else {
        saveNotes();
        const auto previousId = selectedId_;
        const bool restoreSelection = inspector_->isVisible();
        galleryModel_->setPhotos(r.photos);
        selectedId_ = 0;
        inspector_->hide();
        if (restoreSelection)
            for (int i = 0; i < r.photos.size(); ++i)
                if (r.photos[i].id == previousId) {
                    const auto index = galleryModel_->index(i);
                    if (gallery_->currentIndex() == index)
                        showPhoto(db_->photo(previousId));
                    else
                        gallery_->setCurrentIndex(index);
                    break;
                }
        gallery_->scrollToTop();
    }
    resultTotal_ = r.total;
    resultCursor_ = r.cursor;
    pagingStopped_ = r.error;
    searchPending_ = false;
    stack_->setCurrentIndex(galleryModel_->rowCount() == 0 ? (db_->count() ? 2 : 1) : 0);
    noResultsTitle_->setText(r.error ? QStringLiteral("Search could not finish")
                                     : QStringLiteral("No matches in this view"));
    noResultsHint_->setText(
        r.error ? r.status
                : QStringLiteral("Try fewer words, Smart search, or clear the active filters."));
    noResultsHint_->setWordWrap(true);
    const auto kind = r.ranked                       ? QStringLiteral("ranked images")
                      : searchBox_->text().isEmpty() ? QStringLiteral("images")
                                                     : QStringLiteral("matches");
    resultLabel_->setText(QStringLiteral("%1 %2").arg(QLocale().toString(r.total), kind));
    resultLabel_->setToolTip(r.total > galleryModel_->rowCount()
                                 ? QStringLiteral("%1 loaded · keep scrolling for more")
                                       .arg(QLocale().toString(galleryModel_->rowCount()))
                                 : QString());
    const bool activeSearch =
        !searchBox_->text().trimmed().isEmpty() || similarId_ || !referencePath_.isEmpty();
    status_->setText(r.error || activeSearch ? r.status : QString());
    status_->setToolTip(r.status + QStringLiteral(" · %1 ms").arg(r.elapsedMs));
    schedulePrefetch();
}
void Window::chooseFolder() {
    if (indexing_) {
        status_->setText(QStringLiteral("Pause the current scan before adding another folder."));
        return;
    }
    const auto folder =
        QFileDialog::getExistingDirectory(this, QStringLiteral("Choose a folder of images"));
    if (!folder.isEmpty())
        addFolder(folder);
}
void Window::addFolder(const QString &input) {
    const auto path = normalizedPath(input);
    for (const auto &existing : db_->folders()) {
        if (path == existing || path.startsWith(existing + u'/', Qt::CaseInsensitive) ||
            existing.startsWith(path + u'/', Qt::CaseInsensitive)) {
            status_->setText(
                QStringLiteral("This folder overlaps a connected folder. Choose a separate folder "
                               "or disconnect the existing one first."));
            return;
        }
    }
    db_->addFolder(path);
    refreshFolders();
    scan();
}
void Window::setQuery(const QString &query) {
    searchBox_->setText(query);
}
void Window::scan(bool force, bool appearanceOnly) {
    if (indexing_ || db_->folders().isEmpty())
        return;
    if (!appearanceOnly && !modelsPresent(paths_.models())) {
        settingsDialog();
        return;
    }
    indexing_ = true;
    indexingBackend_.clear();
    scanElapsed_.start();
    indexer_->prepare();
    indexPanel_->show();
    cancelButton_->setEnabled(true);
    scanButton_->setEnabled(false);
    addButton_->setEnabled(false);
    progress_->setRange(0, 0);
    auto folders = db_->folders();
    const bool ocr = settings_.value(QStringLiteral("library/ocr"), true).toBool();
    const bool useGpu = settings_.value(QStringLiteral("library/gpu"), true).toBool();
    QMetaObject::invokeMethod(
        indexer_,
        [worker = indexer_, folders, ocr, force, appearanceOnly, useGpu] {
            worker->scan(folders, ocr, force, appearanceOnly, useGpu);
        },
        Qt::QueuedConnection);
}
void Window::showPhoto(const Photo &p) {
    saveNotes();
    if (!p.id)
        return;
    selectedId_ = p.id;
    detailName_->setText(p.name);
    detailName_->setToolTip(p.path);
    detailMeta_->setText(
        QStringLiteral("%1 × %2 px · %3").arg(p.width).arg(p.height).arg(fileSize(p.bytes)));
    detailMeta_->setToolTip(QStringLiteral("Modified %1")
                                .arg(QDateTime::fromMSecsSinceEpoch(p.modified)
                                         .toString(QStringLiteral("MMM d, yyyy"))));
    auto pix = QPixmap::fromImage(db_->thumbnail(p.thumb));
    preview_->setPixmap(
        pix.scaled(preview_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    detailTags_->setText(p.tags);
    detailTags_->parentWidget()->setVisible(!p.tags.isEmpty());
    detailOcr_->setText(p.ocr.left(1800));
    detailOcr_->parentWidget()->setVisible(!p.ocr.isEmpty());
    const QSignalBlocker notesBlock(notes_);
    notes_->setPlainText(p.notes);
    favoriteDetail_->setText(p.favorite ? QStringLiteral("Saved to favorites")
                                        : QStringLiteral("Add to favorites"));
    inspector_->show();
}
void Window::toggleFavorite(qint64 id) {
    if (!id)
        return;
    auto p = db_->photo(id);
    p.favorite = !p.favorite;
    db_->setFavorite(id, p.favorite);
    galleryModel_->updatePhoto(p);
    if (selectedId_ == id)
        favoriteDetail_->setText(p.favorite ? QStringLiteral("Saved to favorites")
                                            : QStringLiteral("Add to favorites"));
    QMetaObject::invokeMethod(
        searcher_, [worker = searcher_, p] { worker->updateMetadata(p); }, Qt::QueuedConnection);
    if (favorites_)
        requestSearch();
}
void Window::saveNotes() {
    notesTimer_.stop();
    if (!selectedId_ || !notesDirty_)
        return;
    db_->setNotes(selectedId_, notes_->toPlainText());
    auto p = db_->photo(selectedId_);
    galleryModel_->updatePhoto(p);
    notesDirty_ = false;
    QMetaObject::invokeMethod(
        searcher_, [worker = searcher_, p] { worker->updateMetadata(p); }, Qt::QueuedConnection);
    status_->setText(QStringLiteral("Description saved"));
}
void Window::openViewer() {
    if (!selectedId_)
        return;
    saveNotes();
    ImageViewer viewer(galleryModel_->photos(), selectedId_, this);
    viewer.exec();
}
void Window::showError(const QString &title, const QString &text) {
    QMessageBox::warning(this, title, text);
}
void Window::showDetailsText(const QString &title, const QString &text) {
    auto *d = new QDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    d->setWindowTitle(title);
    d->resize(720, 550);
    auto *l = new QVBoxLayout(d);
    auto *edit = new QPlainTextEdit(text);
    edit->setReadOnly(true);
    l->addWidget(edit);
    auto *close = button(QStringLiteral("Close"));
    connect(close, &QPushButton::clicked, d, &QDialog::close);
    l->addWidget(close, 0, Qt::AlignRight);
    d->show();
}

void Window::updateReference() {
    const auto generation = ++referenceGeneration_;
    const bool active = similarId_ || !referencePath_.isEmpty();
    referencePanel_->setVisible(active);
    clearButton_->setVisible(active || !searchBox_->text().isEmpty());
    searchMode_->setEnabled(!active);
    if (!active)
        return;
    const QSignalBlocker blocker(similarityMode_);
    similarityMode_->setCurrentIndex(similarityMode_->findData(int(similarity_)));
    const auto photo = similarId_ ? db_->photo(similarId_) : Photo{};
    const auto name = similarId_ ? photo.name : QFileInfo(referencePath_).fileName();
    referenceName_->setText(QStringLiteral("Matching  ") + name);
    referenceName_->setToolTip(name);
    referencePreview_->clear();
    if (referenceWatcher_)
        return; // Coalesce rapid reference changes into one in-flight decode.
    auto *watcher = new QFutureWatcher<QImage>(this);
    referenceWatcher_ = watcher;
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, generation] {
        const auto image = watcher->result();
        watcher->deleteLater();
        referenceWatcher_ = nullptr;
        if (generation != referenceGeneration_) {
            if (similarId_ || !referencePath_.isEmpty())
                updateReference();
            return;
        }
        referencePreview_->setPixmap(QPixmap::fromImage(image).scaled(42, 42, Qt::KeepAspectRatio,
                                                                      Qt::SmoothTransformation));
    });
    watcher->setFuture(QtConcurrent::run([paths = paths_, thumbnail = photo.thumb,
                                          path = referencePath_, indexed = bool(similarId_)] {
        return indexed ? readThumbnail(paths, thumbnail) : readImage(path, 84);
    }));
}
void Window::startSimilarity(qint64 id, SimilarityMode mode) {
    if (!id)
        return;
    saveNotes();
    const QSignalBlocker queryBlock(searchBox_), sortBlock(sortOrder_);
    searchBox_->clear();
    referencePath_.clear();
    similarId_ = id;
    similarity_ = mode;
    sortOrder_->setCurrentIndex(0);
    updateReference();
    requestSearch();
}
void Window::searchImage(const QString &path) {
    saveNotes();
    const QSignalBlocker queryBlock(searchBox_), sortBlock(sortOrder_);
    searchBox_->clear();
    similarId_ = 0;
    referencePath_ = normalizedPath(path);
    similarity_ = SimilarityMode::Appearance;
    sortOrder_->setCurrentIndex(0);
    updateReference();
    requestSearch();
}
void Window::dragEnterEvent(QDragEnterEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.first().isLocalFile() &&
        QFileInfo(urls.first().toLocalFile()).isFile())
        event->acceptProposedAction();
}
void Window::dropEvent(QDropEvent *event) {
    const auto urls = event->mimeData()->urls();
    if (urls.size() == 1 && urls.first().isLocalFile()) {
        searchImage(urls.first().toLocalFile());
        event->acceptProposedAction();
    }
}
} // namespace piclocate
