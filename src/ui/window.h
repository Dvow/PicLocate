#pragma once
#include "models/model_download.h"
#include "ui/gallery.h"
#include "updates/update.h"
namespace piclocate {
class Database;
class Indexer;
class Searcher;
class Window : public QMainWindow {
    Q_OBJECT
  public:
    explicit Window(Paths paths, UpdateSource updates = AppUpdate::defaultSource());
    ~Window() override;
    void addFolder(const QString &path);
    void setQuery(const QString &query);
    void searchImage(const QString &path);

  protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    Paths paths_;
    std::unique_ptr<Database> db_;
    QSettings settings_;
    QThread indexThread_, searchThread_;
    Indexer *indexer_;
    Searcher *searcher_;
    ModelDownload download_;
    AppUpdate updater_;
    QTimer debounce_, rescanTimer_, notesTimer_, prefetchTimer_;
    QElapsedTimer scanElapsed_;
    quint64 generation_ = 0;
    quint64 referenceGeneration_ = 0;
    QFutureWatcher<QImage> *referenceWatcher_ = nullptr;
    bool indexing_ = false, favorites_ = false, notesDirty_ = false;
    int resultTotal_ = 0;
    quint64 resultCursor_ = 0;
    bool searchPending_ = false, pagingStopped_ = false;
    QString folder_, indexingBackend_, referencePath_;
    qint64 similarId_ = 0, selectedId_ = 0;
    SimilarityMode similarity_ = SimilarityMode::Subject;
    QLineEdit *searchBox_;
    QLabel *title_, *subtitle_, *librarySummary_, *status_, *modelState_, *resultLabel_,
        *indexLabel_, *referenceName_, *referencePreview_, *noResultsTitle_, *noResultsHint_,
        *pageStatus_;
    QPushButton *allButton_, *favoritesButton_, *scanButton_, *addButton_, *cancelButton_,
        *clearButton_;
    QListWidget *folders_;
    QComboBox *orientation_, *searchMode_, *sortOrder_, *format_, *similarityMode_;
    QListView *gallery_;
    GalleryModel *galleryModel_;
    QStackedWidget *stack_;
    QWidget *empty_, *inspector_, *indexPanel_, *referencePanel_, *searchPanel_, *resultsToolbar_,
        *thumbnailControls_;
    QProgressBar *progress_;
    QLabel *preview_, *detailName_, *detailMeta_, *detailTags_, *detailOcr_;
    QPlainTextEdit *notes_;
    QPushButton *favoriteDetail_;
    void buildUi();
    QWidget *buildSidebar();
    QWidget *buildGallery();
    QWidget *buildInspector();
    void refreshFolders();
    void refreshStats();
    void requestSearch(bool reload = false, bool more = false);
    void prefetchMore();
    void schedulePrefetch();
    void resetFilters();
    void showPhoto(const Photo &photo);
    void toggleFavorite(qint64 id);
    void scan(bool force = false, bool appearanceOnly = false);
    void chooseFolder();
    void startSimilarity(qint64 id, SimilarityMode mode);
    void updateReference();
    void settingsDialog();
    void checkForUpdates();
    void saveNotes();
    void openViewer();
    void applyResults(const SearchResult &result);
    void setNavigation(bool favorites);
    void showError(const QString &title, const QString &message);
    void showDetailsText(const QString &title, const QString &text);
};
QString styleSheet();
} // namespace piclocate
