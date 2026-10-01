#include "ui/viewer.h"
#include "core/core.h"
#include "ui/ui_helpers.h"
#include "ui/window.h"
#include <QtConcurrent>

namespace piclocate {
ImageCanvas::ImageCanvas(QWidget *parent) : QGraphicsView(parent), scene_(this) {
    setScene(&scene_);
    setDragMode(ScrollHandDrag);
    setTransformationAnchor(AnchorUnderMouse);
    setResizeAnchor(AnchorViewCenter);
    setRenderHint(QPainter::SmoothPixmapTransform);
    setBackgroundBrush(QColor(QStringLiteral("#080e17")));
    setFrameShape(QFrame::NoFrame);
}
void ImageCanvas::setImage(const QImage &image) {
    scene_.clear();
    if (!image.isNull())
        scene_.addPixmap(QPixmap::fromImage(image));
    scene_.setSceneRect(scene_.itemsBoundingRect());
    fit();
}
void ImageCanvas::fit() {
    fitted_ = true;
    resetTransform();
    if (!scene_.sceneRect().isEmpty())
        fitInView(scene_.sceneRect(), Qt::KeepAspectRatio);
}
void ImageCanvas::zoom(double factor) {
    if (scene_.sceneRect().isEmpty())
        return;
    const double scale = transform().m11() * factor;
    if (scale > .02 && scale < 64) {
        fitted_ = false;
        this->scale(factor, factor);
    }
}
void ImageCanvas::wheelEvent(QWheelEvent *event) {
    zoom(event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2);
    event->accept();
}
void ImageCanvas::resizeEvent(QResizeEvent *event) {
    QGraphicsView::resizeEvent(event);
    if (fitted_)
        fit();
}
ImageViewer::ImageViewer(Photos photos, qint64 selectedId, QWidget *parent)
    : QDialog(parent), photos_(std::move(photos)) {
    setStyleSheet(piclocate::styleSheet());
    resize(1120, 820);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);
    auto *bar = new QHBoxLayout;
    bar->setSpacing(8);
    previous_ = button(QStringLiteral("Previous"));
    next_ = button(QStringLiteral("Next"));
    previous_->setToolTip(QStringLiteral("Previous image · Left arrow"));
    next_->setToolTip(QStringLiteral("Next image · Right arrow"));
    position_ = label({}, QStringLiteral("Muted"));
    bar->addWidget(previous_);
    bar->addWidget(next_);
    bar->addWidget(position_);
    bar->addStretch();
    auto *out = button(QStringLiteral("−"));
    auto *in = button(QStringLiteral("+"));
    out->setFixedSize(36, 36);
    in->setFixedSize(36, 36);
    auto *fit = button(QStringLiteral("Fit"));
    fit->setToolTip(QStringLiteral("Fit image to window · 0"));
    out->setAccessibleName(QStringLiteral("Zoom out"));
    in->setAccessibleName(QStringLiteral("Zoom in"));
    bar->addWidget(out);
    bar->addWidget(in);
    bar->addWidget(fit);
    layout->addLayout(bar);
    canvas_ = new ImageCanvas;
    canvas_->setAccessibleName(QStringLiteral("Image preview. Scroll to zoom and drag to pan."));
    layout->addWidget(canvas_, 1);
    caption_ = label({}, QStringLiteral("Muted"));
    caption_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    caption_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(caption_);
    auto previous = [this] { showIndex(index_ - 1); };
    auto next = [this] { showIndex(index_ + 1); };
    connect(previous_, &QPushButton::clicked, this, previous);
    connect(next_, &QPushButton::clicked, this, next);
    connect(out, &QPushButton::clicked, this, [this] { canvas_->zoom(1.0 / 1.25); });
    connect(in, &QPushButton::clicked, this, [this] { canvas_->zoom(1.25); });
    connect(fit, &QPushButton::clicked, this, [this] { canvas_->fit(); });
    connect(new QShortcut(QKeySequence(Qt::Key_Left), this), &QShortcut::activated, this, previous);
    connect(new QShortcut(QKeySequence(Qt::Key_Right), this), &QShortcut::activated, this, next);
    connect(new QShortcut(QKeySequence(Qt::Key_0), this), &QShortcut::activated, this,
            [this] { canvas_->fit(); });
    for (int i = 0; i < photos_.size(); ++i)
        if (photos_[i].id == selectedId)
            index_ = i;
    showIndex(index_);
}
void ImageViewer::showIndex(int index) {
    if (index < 0 || index >= photos_.size())
        return;
    index_ = index;
    const auto photo = photos_[index_];
    const auto generation = ++generation_;
    setWindowTitle(photo.name);
    position_->setText(QStringLiteral("%1 / %2").arg(index_ + 1).arg(photos_.size()));
    previous_->setEnabled(index_ > 0);
    next_->setEnabled(index_ + 1 < photos_.size());
    caption_->setText(QStringLiteral("Loading image…"));
    canvas_->setImage({});
    if (watcher_)
        return; // Coalesce rapid navigation to the latest image; keep only one decode in flight.
    auto *watcher = new QFutureWatcher<QImage>(this);
    watcher_ = watcher;
    connect(watcher, &QFutureWatcher<QImage>::finished, this, [this, watcher, photo, generation] {
        const auto image = watcher->result();
        watcher->deleteLater();
        watcher_ = nullptr;
        if (generation != generation_) {
            showIndex(index_);
            return;
        }
        canvas_->setImage(image);
        caption_->setText(
            image.isNull()
                ? QStringLiteral(
                      "Original unavailable. The file may have moved or its drive is disconnected.")
                : photo.name);
        caption_->setToolTip(QDir::toNativeSeparators(photo.path));
    });
    watcher->setFuture(QtConcurrent::run([path = photo.path] { return readImage(path, 3000); }));
}
} // namespace piclocate
