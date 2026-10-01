#include "library/database.h"
#include "library/indexer.h"
#include "library/search.h"
#include "ui/ui_helpers.h"
#include "ui/window.h"
#include <QDesktopServices>

namespace piclocate {
void Window::buildUi() {
    auto *root = new QWidget;
    auto *layout = new QHBoxLayout(root);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    setCentralWidget(root);
    layout->addWidget(buildSidebar());
    auto *main = new QWidget;
    auto *mainLayout = new QVBoxLayout(main);
    mainLayout->setContentsMargins(24, 24, 24, 16);
    mainLayout->setSpacing(16);
    layout->addWidget(main, 1);
    auto *header = new QHBoxLayout;
    auto *headText = new QVBoxLayout;
    header->setSpacing(8);
    headText->setSpacing(4);
    title_ = label(QStringLiteral("All images"), QStringLiteral("PageTitle"));
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    librarySummary_ = label({}, QStringLiteral("Muted"));
    librarySummary_->setObjectName(QStringLiteral("LibrarySummary"));
    headText->addWidget(title_);
    headText->addWidget(librarySummary_);
    header->addLayout(headText, 1);
    scanButton_ = button(QStringLiteral("Rescan"), QStringLiteral("refresh"));
    scanButton_->setObjectName(QStringLiteral("RescanButton"));
    scanButton_->setToolTip(
        QStringLiteral("Index new and changed images. Unchanged images are skipped."));
    auto *scanMenu = new QMenu(scanButton_);
    scanMenu->addAction(QStringLiteral("Scan new and changed images"), this, [this] { scan(); });
    scanMenu->addAction(QStringLiteral("Build appearance index (fast)"), this,
                        [this] { scan(false, true); });
    scanButton_->setMenu(scanMenu);
    header->addWidget(scanButton_);
    addButton_ =
        button(QStringLiteral("Add folder"), QStringLiteral("plus"), QStringLiteral("Primary"));
    addButton_->setIcon(icon(QStringLiteral("plus"), QColor(QStringLiteral("#1a2c49"))));
    connect(addButton_, &QPushButton::clicked, this, &Window::chooseFolder);
    header->addWidget(addButton_);
    mainLayout->addLayout(header);
    subtitle_ = label({}, QStringLiteral("Muted"));
    subtitle_->setObjectName(QStringLiteral("LibraryNotice"));
    subtitle_->setWordWrap(true);
    subtitle_->hide();
    mainLayout->addWidget(subtitle_);
    auto *searchFrame = new QFrame;
    searchPanel_ = searchFrame;
    searchFrame->setObjectName(QStringLiteral("Search"));
    auto *searchLayout = new QHBoxLayout(searchFrame);
    searchLayout->setContentsMargins(12, 6, 8, 6);
    searchLayout->setSpacing(8);
    auto *searchIcon = label(QString());
    searchIcon->setPixmap(
        icon(QStringLiteral("search"), QColor(QStringLiteral("#a8c7fa"))).pixmap(23, 23));
    searchLayout->addWidget(searchIcon);
    searchBox_ = new QLineEdit;
    searchBox_->setObjectName(QStringLiteral("SearchBox"));
    searchBox_->setAcceptDrops(false);
    searchBox_->setPlaceholderText(QStringLiteral("Search images…"));
    searchBox_->setToolTip(QStringLiteral(
        "Search by description, filename or text in an image. Ctrl+K focuses search."));
    searchBox_->setAccessibleName(QStringLiteral("Search your image library"));
    searchBox_->setMaxLength(1200);
    searchLayout->addWidget(searchBox_, 1);
    searchMode_ = new QComboBox;
    searchMode_->setObjectName(QStringLiteral("SearchMode"));
    searchMode_->setAccessibleName(QStringLiteral("Search mode"));
    searchMode_->addItems({QStringLiteral("Smart search"), QStringLiteral("Visual only"),
                           QStringLiteral("Text only")});
    searchMode_->setToolTip(
        QStringLiteral("Smart combines visual similarity and text. Visual uses image content. Text "
                       "searches filenames, labels, OCR and descriptions. Text matches require all "
                       "words; use quotes for phrases or sword* for a prefix."));
    searchLayout->addWidget(searchMode_);
    auto *byImage = button(QString(), QStringLiteral("image"), QStringLiteral("Quiet"));
    byImage->setObjectName(QStringLiteral("SearchByImage"));
    byImage->setFixedSize(36, 36);
    byImage->setAccessibleName(QStringLiteral("Search by image"));
    byImage->setToolTip(
        QStringLiteral("Search by a local image · or drop an image anywhere in PicLocate"));
    connect(byImage, &QPushButton::clicked, this, [this] {
        const auto path = QFileDialog::getOpenFileName(
            this, QStringLiteral("Choose a reference image"), {},
            QStringLiteral("Images (*.png *.jpg *.jpeg *.webp *.bmp *.gif *.tif *.tiff *.svg);;All "
                           "files (*)"));
        if (!path.isEmpty())
            searchImage(path);
    });
    searchLayout->addWidget(byImage);
    connect(searchMode_, &QComboBox::currentIndexChanged, this, [this] { requestSearch(); });
    clearButton_ = button(QStringLiteral("Clear"), {}, QStringLiteral("Quiet"));
    clearButton_->hide();
    connect(clearButton_, &QPushButton::clicked, this, [this] {
        similarId_ = 0;
        referencePath_.clear();
        searchBox_->clear();
        updateReference();
        requestSearch();
    });
    searchLayout->addWidget(clearButton_);
    mainLayout->addWidget(searchFrame);
    connect(searchBox_, &QLineEdit::textChanged, this, [this] {
        similarId_ = 0;
        referencePath_.clear();
        updateReference();
        ++generation_;
        searcher_->setLatest(generation_);
        debounce_.start();
    });
    connect(searchBox_, &QLineEdit::returnPressed, this, [this] {
        debounce_.stop();
        requestSearch();
    });
    auto *shortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(shortcut, &QShortcut::activated, this, [this] {
        searchBox_->setFocus();
        searchBox_->selectAll();
    });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] {
        if (!searchBox_->text().isEmpty() || similarId_ || !referencePath_.isEmpty()) {
            similarId_ = 0;
            referencePath_.clear();
            searchBox_->clear();
            updateReference();
            requestSearch();
        } else {
            saveNotes();
            inspector_->hide();
        }
    });
    referencePanel_ = new QWidget;
    referencePanel_->setObjectName(QStringLiteral("ReferencePanel"));
    auto *referenceLayout = new QHBoxLayout(referencePanel_);
    referenceLayout->setContentsMargins(12, 7, 12, 7);
    referencePreview_ = label({});
    referencePreview_->setObjectName(QStringLiteral("ReferencePreview"));
    referencePreview_->setFixedSize(42, 42);
    referencePreview_->setAlignment(Qt::AlignCenter);
    referenceLayout->addWidget(referencePreview_);
    referenceName_ = label({}, QStringLiteral("ReferenceName"));
    referenceName_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    referenceLayout->addWidget(referenceName_, 1);
    similarityMode_ = new QComboBox;
    similarityMode_->setObjectName(QStringLiteral("SimilarityMode"));
    similarityMode_->setAccessibleName(QStringLiteral("What to match in the reference image"));
    similarityMode_->addItem(QStringLiteral("Overall appearance"), int(SimilarityMode::Appearance));
    similarityMode_->addItem(QStringLiteral("Shape and detail"), int(SimilarityMode::Shape));
    similarityMode_->addItem(QStringLiteral("Color palette"), int(SimilarityMode::Color));
    similarityMode_->addItem(QStringLiteral("Similar subjects"), int(SimilarityMode::Subject));
    similarityMode_->setToolTip(
        QStringLiteral("Overall: color + structure. Shape: silhouettes and edges with less color "
                       "influence. Color: palette and its layout. Subjects: the kind of object."));
    referenceLayout->addWidget(similarityMode_);
    connect(similarityMode_, &QComboBox::currentIndexChanged, this, [this] {
        similarity_ = static_cast<SimilarityMode>(similarityMode_->currentData().toInt());
        requestSearch();
    });
    referencePanel_->hide();
    mainLayout->addWidget(referencePanel_);
    indexPanel_ = new QWidget;
    indexPanel_->setObjectName(QStringLiteral("IndexPanel"));
    auto *indexLayout = new QHBoxLayout(indexPanel_);
    auto *progressLayout = new QVBoxLayout;
    indexLabel_ = label(QStringLiteral("Preparing your library…"));
    indexLabel_->setWordWrap(true);
    indexLabel_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    progressLayout->addWidget(indexLabel_);
    progress_ = new QProgressBar;
    progress_->setTextVisible(false);
    progress_->setFixedHeight(5);
    progressLayout->addWidget(progress_);
    indexLayout->addLayout(progressLayout, 1);
    cancelButton_ = button(QStringLiteral("Pause"), {}, QStringLiteral("Quiet"));
    connect(cancelButton_, &QPushButton::clicked, this, [this] {
        indexer_->cancel();
        cancelButton_->setEnabled(false);
        indexLabel_->setText(QStringLiteral("Saving progress after this image…"));
    });
    indexLayout->addWidget(cancelButton_);
    mainLayout->addWidget(indexPanel_);
    indexPanel_->hide();
    resultsToolbar_ = new QWidget;
    auto *resultsControls = new QVBoxLayout(resultsToolbar_);
    resultsControls->setContentsMargins(0, 0, 0, 0);
    resultsControls->setSpacing(12);
    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(8);
    resultLabel_ = label({}, QStringLiteral("Muted"));
    resultLabel_->setObjectName(QStringLiteral("ResultCount"));
    toolbar->addWidget(resultLabel_);
    toolbar->addStretch();
    sortOrder_ = new QComboBox;
    sortOrder_->setObjectName(QStringLiteral("SortOrder"));
    sortOrder_->setAccessibleName(QStringLiteral("Sort images"));
    sortOrder_->addItems({QStringLiteral("Best match"), QStringLiteral("Newest first"),
                          QStringLiteral("Oldest first"), QStringLiteral("Name A–Z"),
                          QStringLiteral("Largest first")});
    connect(sortOrder_, &QComboBox::currentIndexChanged, this, [this] { requestSearch(); });
    toolbar->addWidget(sortOrder_);
    auto *filters = button(QStringLiteral("Filters"), QStringLiteral("filters"));
    filters->setObjectName(QStringLiteral("ToggleFilters"));
    filters->setAccessibleName(QStringLiteral("Show shape and format filters"));
    filters->setCheckable(true);
    toolbar->addWidget(filters);
    resultsControls->addLayout(toolbar);
    auto *filterPanel = new QWidget;
    filterPanel->setObjectName(QStringLiteral("FilterPanel"));
    auto *filterLayout = new QHBoxLayout(filterPanel);
    filterLayout->setContentsMargins(0, 0, 0, 0);
    filterLayout->setSpacing(8);
    orientation_ = new QComboBox;
    orientation_->setObjectName(QStringLiteral("ShapeFilter"));
    orientation_->setAccessibleName(QStringLiteral("Filter by image shape"));
    orientation_->addItems({QStringLiteral("All shapes"), QStringLiteral("Landscape"),
                            QStringLiteral("Portrait"), QStringLiteral("Square")});
    connect(orientation_, &QComboBox::currentIndexChanged, this, [this] { requestSearch(); });
    filterLayout->addWidget(orientation_);
    format_ = new QComboBox;
    format_->setObjectName(QStringLiteral("FormatFilter"));
    format_->setAccessibleName(QStringLiteral("Filter by image format"));
    format_->addItems({QStringLiteral("All formats"), QStringLiteral("PNG"), QStringLiteral("JPG"),
                       QStringLiteral("JPEG"), QStringLiteral("WEBP"), QStringLiteral("GIF"),
                       QStringLiteral("TIFF"), QStringLiteral("BMP"), QStringLiteral("SVG")});
    connect(format_, &QComboBox::currentIndexChanged, this, [this] { requestSearch(); });
    filterLayout->addWidget(format_);
    auto *reset = button(QStringLiteral("Reset"), {}, QStringLiteral("Quiet"));
    reset->setObjectName(QStringLiteral("ResetFilters"));
    reset->setAccessibleName(QStringLiteral("Reset filters"));
    connect(reset, &QPushButton::clicked, this, &Window::resetFilters);
    filterLayout->addWidget(reset);
    filterLayout->addStretch();
    const auto updateFilters = [filters, reset, shape = orientation_, format = format_] {
        const int active = int(shape->currentIndex() != 0) + int(format->currentIndex() != 0);
        filters->setText(active ? QStringLiteral("Filters (%1)").arg(active)
                                : QStringLiteral("Filters"));
        reset->setVisible(active != 0);
    };
    connect(orientation_, &QComboBox::currentIndexChanged, this, updateFilters);
    connect(format_, &QComboBox::currentIndexChanged, this, updateFilters);
    connect(filters, &QPushButton::toggled, filterPanel, &QWidget::setVisible);
    updateFilters();
    filterPanel->hide();
    resultsControls->addWidget(filterPanel);
    mainLayout->addWidget(resultsToolbar_);
    auto *body = new QHBoxLayout;
    body->setSpacing(16);
    body->addWidget(buildGallery(), 1);
    body->addWidget(buildInspector());
    mainLayout->addLayout(body, 1);
    auto *footer = new QHBoxLayout;
    footer->setSpacing(12);
    status_ = label({}, QStringLiteral("Muted"));
    status_->setObjectName(QStringLiteral("SearchStatus"));
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    status_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    footer->addWidget(status_, 1);
    modelState_ = label({}, QStringLiteral("Muted"));
    footer->addWidget(modelState_);
    thumbnailControls_ = new QWidget;
    auto *thumbnailLayout = new QHBoxLayout(thumbnailControls_);
    thumbnailLayout->setContentsMargins(0, 0, 0, 0);
    thumbnailLayout->setSpacing(8);
    thumbnailLayout->addWidget(label(QStringLiteral("Thumbnails"), QStringLiteral("Muted")));
    auto *density = new QSlider(Qt::Horizontal);
    density->setObjectName(QStringLiteral("ThumbnailSize"));
    density->setAccessibleName(QStringLiteral("Thumbnail size"));
    density->setRange(150, 300);
    density->setSingleStep(10);
    density->setFixedWidth(100);
    density->setValue(galleryModel_->cardWidth());
    density->setToolTip(QStringLiteral("Adjust thumbnail size. Smaller cards show more assets."));
    connect(density, &QSlider::valueChanged, this, [this](int width) {
        galleryModel_->setCardSize(width);
        gallery_->doItemsLayout();
        settings_.setValue(QStringLiteral("view/cardWidth"), width);
        schedulePrefetch();
    });
    thumbnailLayout->addWidget(density);
    footer->addWidget(thumbnailControls_);
    pageStatus_ = label(QString(), QStringLiteral("Muted"));
    pageStatus_->setObjectName(QStringLiteral("PageStatus"));
    pageStatus_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pageStatus_->hide();
    footer->addWidget(pageStatus_);
    mainLayout->addLayout(footer);
}

QWidget *Window::buildSidebar() {
    auto *side = new QWidget;
    side->setObjectName(QStringLiteral("Sidebar"));
    side->setFixedWidth(208);
    auto *sidebarLayout = new QVBoxLayout(side);
    sidebarLayout->setContentsMargins(16, 24, 16, 16);
    sidebarLayout->setSpacing(8);
    auto *brandRow = new QHBoxLayout;
    auto *mark = label(QString());
    mark->setPixmap(
        icon(QStringLiteral("spark"), QColor(QStringLiteral("#a8c7fa"))).pixmap(30, 30));
    brandRow->addWidget(mark);
    brandRow->addWidget(label(QStringLiteral("PicLocate"), QStringLiteral("Brand")));
    brandRow->addStretch();
    sidebarLayout->addLayout(brandRow);
    sidebarLayout->addSpacing(24);
    allButton_ =
        button(QStringLiteral("All images"), QStringLiteral("grid"), QStringLiteral("Nav"));
    favoritesButton_ =
        button(QStringLiteral("Favorites"), QStringLiteral("heart"), QStringLiteral("Nav"));
    allButton_->setCheckable(true);
    favoritesButton_->setCheckable(true);
    allButton_->setProperty("navigation", true);
    favoritesButton_->setProperty("navigation", true);
    allButton_->setChecked(true);
    sidebarLayout->addWidget(allButton_);
    sidebarLayout->addWidget(favoritesButton_);
    connect(allButton_, &QPushButton::clicked, this, [this] { setNavigation(false); });
    connect(favoritesButton_, &QPushButton::clicked, this, [this] { setNavigation(true); });
    sidebarLayout->addSpacing(16);
    auto *folderTitle = new QHBoxLayout;
    folderTitle->addWidget(label(QStringLiteral("Folders"), QStringLiteral("SectionLabel")));
    folderTitle->addStretch();
    sidebarLayout->addLayout(folderTitle);
    folders_ = new QListWidget;
    folders_->setObjectName(QStringLiteral("Folders"));
    folders_->setAccessibleName(QStringLiteral("Connected folders"));
    folders_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    folders_->setTextElideMode(Qt::ElideRight);
    folders_->setContextMenuPolicy(Qt::CustomContextMenu);
    sidebarLayout->addWidget(folders_, 1);
    connect(folders_, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        folder_ = item->data(Qt::UserRole).toString();
        favorites_ = false;
        allButton_->setChecked(false);
        favoritesButton_->setChecked(false);
        const auto name = QFileInfo(folder_).fileName();
        title_->setText(name.isEmpty() ? QDir::toNativeSeparators(folder_) : name);
        title_->setToolTip(QDir::toNativeSeparators(folder_));
        requestSearch();
    });
    connect(folders_, &QListWidget::customContextMenuRequested, this, [this](QPoint point) {
        auto *item = folders_->itemAt(point);
        if (!item)
            return;
        QMenu menu;
        auto *remove = menu.addAction(QStringLiteral("Disconnect folder from library"));
        remove->setEnabled(!indexing_);
        if (menu.exec(folders_->mapToGlobal(point)) == remove) {
            db_->removeFolder(item->data(Qt::UserRole).toString());
            folder_.clear();
            refreshFolders();
            refreshStats();
            setNavigation(false);
            requestSearch(true);
        }
    });
    auto *settings =
        button(QStringLiteral("Settings"), QStringLiteral("settings"), QStringLiteral("Nav"));
    settings->setObjectName(QStringLiteral("SettingsButton"));
    settings->setProperty("navigation", true);
    connect(settings, &QPushButton::clicked, this, &Window::settingsDialog);
    sidebarLayout->addWidget(settings);
    return side;
}

QWidget *Window::buildGallery() {
    stack_ = new QStackedWidget;
    galleryModel_ = new GalleryModel(this, paths_);
    galleryModel_->setCardSize(settings_.value(QStringLiteral("view/cardWidth"), 210).toInt());
    gallery_ = new QListView;
    gallery_->setObjectName(QStringLiteral("Gallery"));
    gallery_->setAccessibleName(QStringLiteral("Image search results"));
    gallery_->setViewMode(QListView::IconMode);
    gallery_->setResizeMode(QListView::Adjust);
    gallery_->setMovement(QListView::Static);
    // Uniform-size layout is cheap and updates the scroll range atomically.
    // Batched layout temporarily shrinks that range on append, snapping the
    // viewport back toward the top even though no model reset occurred.
    gallery_->setLayoutMode(QListView::SinglePass);
    gallery_->setUniformItemSizes(true);
    gallery_->setWrapping(true);
    gallery_->setSpacing(0);
    gallery_->setMouseTracking(true);
    gallery_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    gallery_->setSelectionMode(QAbstractItemView::SingleSelection);
    gallery_->setModel(galleryModel_);
    gallery_->viewport()->installEventFilter(this);
    connect(gallery_->verticalScrollBar(), &QScrollBar::valueChanged, this,
            [this] { schedulePrefetch(); });
    connect(gallery_->verticalScrollBar(), &QScrollBar::rangeChanged, this,
            [this] { schedulePrefetch(); });
    auto *thumbnailRefresh = new QTimer(this);
    thumbnailRefresh->setSingleShot(true);
    thumbnailRefresh->setInterval(16);
    connect(galleryModel_, &GalleryModel::thumbnailReady, thumbnailRefresh, [thumbnailRefresh] {
        if (!thumbnailRefresh->isActive())
            thumbnailRefresh->start();
    });
    connect(thumbnailRefresh, &QTimer::timeout, this, [this] {
        gallery_->viewport()->update();
        schedulePrefetch();
    });
    auto *delegate = new GalleryDelegate(gallery_);
    gallery_->setItemDelegate(delegate);
    connect(delegate, &GalleryDelegate::favoriteClicked, this, &Window::toggleFavorite);
    connect(gallery_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &i) {
                if (i.isValid())
                    showPhoto(db_->photo(galleryModel_->photo(i.row()).id));
            });
    connect(gallery_, &QListView::doubleClicked, this, [this] { openViewer(); });
    auto *previewShortcut = new QShortcut(QKeySequence(Qt::Key_Space), gallery_);
    previewShortcut->setContext(Qt::WidgetShortcut);
    connect(previewShortcut, &QShortcut::activated, this, &Window::openViewer);
    gallery_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(gallery_, &QListView::customContextMenuRequested, this, [this](QPoint point) {
        const auto index = gallery_->indexAt(point);
        if (!index.isValid())
            return;
        gallery_->setCurrentIndex(index);
        const auto photo = galleryModel_->photo(index.row());
        QMenu menu;
        menu.addAction(QStringLiteral("Preview · Space"), this, &Window::openViewer);
        menu.addAction(QStringLiteral("Match appearance"), this,
                       [this, photo] { startSimilarity(photo.id, SimilarityMode::Appearance); });
        menu.addAction(QStringLiteral("Match shape and detail"), this,
                       [this, photo] { startSimilarity(photo.id, SimilarityMode::Shape); });
        menu.addAction(QStringLiteral("Find similar subjects"), this,
                       [this, photo] { startSimilarity(photo.id, SimilarityMode::Subject); });
        menu.addSeparator();
        menu.addAction(photo.favorite ? QStringLiteral("Remove favorite")
                                      : QStringLiteral("Add favorite"),
                       this, [this, photo] { toggleFavorite(photo.id); });
        menu.addAction(QStringLiteral("Copy file path"), this, [photo] {
            QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(photo.path));
        });
        menu.exec(gallery_->viewport()->mapToGlobal(point));
    });
    stack_->addWidget(gallery_);
    empty_ = new QWidget;
    auto *emptyLayout = new QVBoxLayout(empty_);
    emptyLayout->setAlignment(Qt::AlignCenter);
    emptyLayout->setSpacing(13);
    emptyLayout->addWidget(new EmptyArt, 0, Qt::AlignHCenter);
    auto *welcome = label(QStringLiteral("Find your images"), QStringLiteral("WelcomeTitle"));
    welcome->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(welcome);
    auto *description =
        label(QStringLiteral("Add a folder to search by description, filename or image."),
              QStringLiteral("Muted"));
    description->setAlignment(Qt::AlignCenter);
    description->setWordWrap(true);
    emptyLayout->addWidget(description);
    emptyLayout->addSpacing(10);
    auto *first = button(QStringLiteral("Choose an image folder"), QStringLiteral("folder"),
                         QStringLiteral("Primary"));
    first->setIcon(icon(QStringLiteral("folder"), QColor(QStringLiteral("#1a2c49"))));
    connect(first, &QPushButton::clicked, this, &Window::chooseFolder);
    emptyLayout->addWidget(first, 0, Qt::AlignHCenter);
    emptyLayout->addSpacing(12);
    auto *promise =
        label(QStringLiteral("Your images stay on this device."), QStringLiteral("Muted"));
    promise->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(promise);
    stack_->addWidget(empty_);
    auto *noResults = new QWidget;
    auto *noResultsLayout = new QVBoxLayout(noResults);
    noResultsLayout->setAlignment(Qt::AlignCenter);
    noResultsTitle_ =
        label(QStringLiteral("No matches in this view"), QStringLiteral("WelcomeTitle"));
    noResultsLayout->addWidget(noResultsTitle_, 0, Qt::AlignCenter);
    noResultsHint_ =
        label(QStringLiteral("Try fewer words, Smart search, or clear the active filters."),
              QStringLiteral("Muted"));
    noResultsHint_->setAlignment(Qt::AlignCenter);
    noResultsLayout->addWidget(noResultsHint_, 0, Qt::AlignCenter);
    auto *resetEmpty =
        button(QStringLiteral("Reset search and filters"), {}, QStringLiteral("Primary"));
    connect(resetEmpty, &QPushButton::clicked, this, [this] {
        searchBox_->clear();
        searchMode_->setCurrentIndex(0);
        resetFilters();
        requestSearch();
    });
    noResultsLayout->addWidget(resetEmpty, 0, Qt::AlignCenter);
    stack_->addWidget(noResults);
    return stack_;
}

QWidget *Window::buildInspector() {
    inspector_ = new QWidget;
    inspector_->setObjectName(QStringLiteral("Inspector"));
    inspector_->setFixedWidth(288);
    auto *inspectorLayout = new QVBoxLayout(inspector_);
    inspectorLayout->setContentsMargins(16, 16, 16, 16);
    inspectorLayout->setSpacing(12);
    auto *detailHeader = new QHBoxLayout;
    detailHeader->addWidget(label(QStringLiteral("Details"), QStringLiteral("SectionLabel")));
    detailHeader->addStretch();
    auto *close = button(QString(), QStringLiteral("close"), QStringLiteral("Quiet"));
    close->setObjectName(QStringLiteral("CloseDetails"));
    close->setFixedSize(32, 32);
    close->setAccessibleName(QStringLiteral("Close image details"));
    connect(close, &QPushButton::clicked, this, [this] {
        saveNotes();
        inspector_->hide();
    });
    detailHeader->addWidget(close);
    inspectorLayout->addLayout(detailHeader);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *detail = new QWidget;
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(0, 0, 0, 0);
    detailLayout->setSpacing(13);
    preview_ = label(QString());
    preview_->setFixedHeight(160);
    preview_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    preview_->setAlignment(Qt::AlignCenter);
    detailLayout->addWidget(preview_);
    detailName_ = label(QString(), QStringLiteral("DetailTitle"));
    detailName_->setWordWrap(true);
    detailName_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    detailLayout->addWidget(detailName_);
    detailMeta_ = label(QString(), QStringLiteral("Muted"));
    detailMeta_->setWordWrap(true);
    detailLayout->addWidget(detailMeta_);
    favoriteDetail_ = button(QStringLiteral("Add to favorites"), QStringLiteral("heart"));
    connect(favoriteDetail_, &QPushButton::clicked, this, [this] { toggleFavorite(selectedId_); });
    detailLayout->addWidget(favoriteDetail_);
    auto *similar = button(QStringLiteral("Find similar subjects"), QStringLiteral("spark"));
    similar->setToolTip(QStringLiteral("Find the same kind of object using local AI."));
    connect(similar, &QPushButton::clicked, this,
            [this] { startSimilarity(selectedId_, SimilarityMode::Subject); });
    detailLayout->addWidget(similar);
    auto *appearance = button(QStringLiteral("Match appearance"), QStringLiteral("search"));
    appearance->setObjectName(QStringLiteral("MatchAppearance"));
    appearance->setToolTip(
        QStringLiteral("Match color, silhouette and edges. Useful for icons, sprites and asset "
                       "variants. Switch to shape or color matching above the results."));
    connect(appearance, &QPushButton::clicked, this,
            [this] { startSimilarity(selectedId_, SimilarityMode::Appearance); });
    detailLayout->addWidget(appearance);
    const auto textSection = [&](const QString &title, QLabel *&value) {
        auto *section = new QWidget;
        auto *sectionLayout = new QVBoxLayout(section);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(6);
        sectionLayout->addWidget(label(title, QStringLiteral("SectionLabel")));
        value = label({}, QStringLiteral("Muted"));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        sectionLayout->addWidget(value);
        detailLayout->addWidget(section);
    };
    textSection(QStringLiteral("Suggested labels"), detailTags_);
    detailTags_->setToolTip(QStringLiteral("AI suggestions may be inaccurate."));
    textSection(QStringLiteral("Text in image"), detailOcr_);
    detailLayout->addWidget(label(QStringLiteral("Description"), QStringLiteral("SectionLabel")));
    notes_ = new QPlainTextEdit;
    notes_->setAcceptDrops(false);
    notes_->setPlaceholderText(QStringLiteral("Add details to make this image easier to find…"));
    notes_->setAccessibleName(QStringLiteral("Your image description"));
    notes_->setFixedHeight(88);
    connect(notes_, &QPlainTextEdit::textChanged, this, [this] {
        if (!selectedId_)
            return;
        notesDirty_ = true;
        notesTimer_.start();
    });
    detailLayout->addWidget(notes_);
    detailLayout->addWidget(
        label(QStringLiteral("Changes save automatically"), QStringLiteral("Muted")));
    detailLayout->addStretch();
    scroll->setWidget(detail);
    inspectorLayout->addWidget(scroll, 1);
    auto *actions = new QHBoxLayout;
    auto *open = button(QStringLiteral("Open"), QStringLiteral("open"));
    auto *reveal = button(QStringLiteral("Show folder"), QStringLiteral("folder"));
    connect(open, &QPushButton::clicked, this, [this] {
        auto p = db_->photo(selectedId_);
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(p.path)))
            showError(QStringLiteral("Could not open image"), p.path);
    });
    connect(reveal, &QPushButton::clicked, this, [this] {
        const auto p = db_->photo(selectedId_);
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p.path).absolutePath()));
    });
    actions->addWidget(open);
    actions->addWidget(reveal);
    inspectorLayout->addLayout(actions);
    inspector_->hide();
    return inspector_;
}
} // namespace piclocate
