#include "ui/gallery.h"
#include "core/core.h"
#include "library/database.h"
namespace piclocate {
QSize galleryCardSize(int viewportWidth, int preferredWidth) {
    const int columns = qMax(1, qRound(double(viewportWidth) / preferredWidth));
    const int width = qMax(120, viewportWidth / columns);
    return {width, width + 5};
}
QIcon icon(const QString &name, const QColor &color) {
    // Painting hundreds of cards should not redraw identical vector icons each frame.
    static QCache<QString, QIcon> cache(128);
    const auto key = name + color.name(QColor::HexArgb);
    if (const auto *cached = cache.object(key))
        return *cached;
    QPixmap pix(40, 40);
    pix.setDevicePixelRatio(2);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == QStringLiteral("search")) {
        p.drawEllipse(QRectF(3, 3, 10, 10));
        p.drawLine(QPointF(11, 11), QPointF(17, 17));
    } else if (name == QStringLiteral("folder")) {
        QPainterPath s;
        s.moveTo(2, 6);
        s.lineTo(2, 4);
        s.lineTo(8, 4);
        s.lineTo(10, 6);
        s.lineTo(18, 6);
        s.lineTo(18, 16);
        s.lineTo(2, 16);
        s.closeSubpath();
        p.drawPath(s);
    } else if (name == QStringLiteral("heart")) {
        QPainterPath s;
        s.moveTo(10, 17);
        s.cubicTo(-4, 8, 4, 0, 10, 6);
        s.cubicTo(16, 0, 24, 8, 10, 17);
        p.drawPath(s);
    } else if (name == QStringLiteral("grid")) {
        for (int x : {3, 11})
            for (int y : {3, 11})
                p.drawRoundedRect(QRectF(x, y, 5, 5), 1, 1);
    } else if (name == QStringLiteral("plus")) {
        p.drawLine(10, 3, 10, 17);
        p.drawLine(3, 10, 17, 10);
    } else if (name == QStringLiteral("refresh")) {
        p.drawArc(QRectF(3, 3, 14, 14), 30 * 16, 290 * 16);
        p.drawLine(16, 2, 17, 7);
        p.drawLine(17, 7, 12, 6);
    } else if (name == QStringLiteral("settings")) {
        p.drawEllipse(QRectF(4, 4, 12, 12));
        p.drawEllipse(QRectF(8, 8, 4, 4));
        for (int a = 0; a < 8; ++a) {
            p.save();
            p.translate(10, 10);
            p.rotate(a * 45);
            p.drawLine(0, -6, 0, -9);
            p.restore();
        }
    } else if (name == QStringLiteral("lock")) {
        p.drawRoundedRect(QRectF(4, 9, 12, 9), 2, 2);
        p.drawArc(QRectF(6, 2, 8, 12), 0, 180 * 16);
        p.drawPoint(10, 13);
    } else if (name == QStringLiteral("close")) {
        p.drawLine(5, 5, 15, 15);
        p.drawLine(5, 15, 15, 5);
    } else if (name == QStringLiteral("filters")) {
        p.drawLine(3, 5, 17, 5);
        p.drawLine(5, 10, 15, 10);
        p.drawLine(8, 15, 12, 15);
    } else if (name == QStringLiteral("spark")) {
        QPainterPath s;
        s.moveTo(10, 1);
        s.lineTo(12.5, 7.5);
        s.lineTo(19, 10);
        s.lineTo(12.5, 12.5);
        s.lineTo(10, 19);
        s.lineTo(7.5, 12.5);
        s.lineTo(1, 10);
        s.lineTo(7.5, 7.5);
        s.closeSubpath();
        p.drawPath(s);
    } else if (name == QStringLiteral("open")) {
        p.drawRect(QRect(3, 7, 10, 10));
        p.drawLine(10, 3, 17, 3);
        p.drawLine(17, 3, 17, 10);
        p.drawLine(8, 12, 17, 3);
    } else if (name == QStringLiteral("image")) {
        p.drawRoundedRect(QRectF(2, 3, 16, 14), 2, 2);
        p.drawEllipse(QRectF(12, 6, 2, 2));
        p.drawPolyline(QPolygonF{{3, 15}, {7, 10}, {10, 13}, {13, 11}, {17, 15}});
    }
    const QIcon result(pix);
    cache.insert(key, new QIcon(result));
    return result;
}
GalleryModel::GalleryModel(QObject *p, Paths paths)
    : QAbstractListModel(p), paths_(std::move(paths)) {
    pool_.setMaxThreadCount(2);
    pool_.setExpiryTimeout(10000);
}
GalleryModel::~GalleryModel() {
    pool_.waitForDone();
}
int GalleryModel::rowCount(const QModelIndex &p) const {
    return p.isValid() ? 0 : int(photos_.size());
}
QVariant GalleryModel::data(const QModelIndex &i, int role) const {
    if (!i.isValid() || i.row() >= photos_.size())
        return {};
    const auto &p = photos_[i.row()];
    if (role == Qt::DisplayRole || role == Qt::AccessibleTextRole)
        return p.name;
    if (role == Qt::ToolTipRole)
        return p.path + QStringLiteral("\n") + QString::number(p.width) + QStringLiteral(" × ") +
               QString::number(p.height);
    return {};
}
void GalleryModel::setPhotos(Photos v) {
    // Loading another page should preserve selection and scroll position.
    const bool append =
        v.size() > photos_.size() && !photos_.isEmpty() &&
        std::equal(photos_.begin(), photos_.end(), v.begin(), [](const Photo &a, const Photo &b) {
            return a.id == b.id && a.thumb == b.thumb && a.favorite == b.favorite &&
                   a.notes == b.notes && a.score == b.score && a.matchReason == b.matchReason;
        });
    if (append) {
        const int oldSize = int(photos_.size());
        beginInsertRows({}, oldSize, int(v.size()) - 1);
        photos_ = std::move(v);
        for (int i = oldSize; i < photos_.size(); ++i)
            thumbnailRows_[photos_[i].thumb.isEmpty() ? photos_[i].path : photos_[i].thumb].append(
                i);
        endInsertRows();
        return;
    }
    beginResetModel();
    photos_ = std::move(v);
    thumbnailRows_.clear();
    for (int i = 0; i < photos_.size(); ++i)
        thumbnailRows_[photos_[i].thumb.isEmpty() ? photos_[i].path : photos_[i].thumb].append(i);
    endResetModel();
}
void GalleryModel::setCardSize(int width) {
    cardWidth_ = qBound(150, width, 300);
}
void GalleryModel::appendPhotos(const Photos &photos) {
    if (photos.isEmpty())
        return;
    const int first = int(photos_.size());
    beginInsertRows({}, first, first + int(photos.size()) - 1);
    photos_.append(photos);
    for (int i = first; i < photos_.size(); ++i)
        thumbnailRows_[photos_[i].thumb.isEmpty() ? photos_[i].path : photos_[i].thumb].append(i);
    endInsertRows();
}
Photo GalleryModel::photo(int i) const {
    return i >= 0 && i < photos_.size() ? photos_[i] : Photo{};
}
void GalleryModel::updatePhoto(const Photo &p) {
    for (int i = 0; i < photos_.size(); ++i)
        if (photos_[i].id == p.id) {
            const auto reason = photos_[i].matchReason;
            photos_[i] = p;
            photos_[i].matchReason = reason;
            emit dataChanged(index(i), index(i));
            break;
        }
}
QSize GalleryDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const {
    const auto *model = qobject_cast<const GalleryModel *>(index.model());
    const int preferred = model ? model->cardWidth() : 210;
    const auto *view = qobject_cast<const QAbstractItemView *>(parent());
    const int available = view ? view->viewport()->width() : preferred;
    return galleryCardSize(available, preferred);
}
QPixmap GalleryModel::thumbnail(int row) const {
    if (row < 0 || row >= photos_.size())
        return {};
    const QString original = photos_[row].path;
    const QString path = photos_[row].thumb.isEmpty() ? original : photos_[row].thumb;
    if (auto *p = cache_.object(path))
        return *p;
    if (pending_.contains(path) || pending_.size() >= 32)
        return {};
    pending_.insert(path);
    auto *self = const_cast<GalleryModel *>(this);
    pool_.start([self, path, original, paths = paths_] {
        QImage image = readThumbnail(paths, path);
        if (image.isNull() && path != original)
            image = readImage(original, 420);
        QMetaObject::invokeMethod(
            self,
            [self, path, image] {
                self->pending_.remove(path);
                if (!image.isNull()) {
                    auto pix = QPixmap::fromImage(image);
                    self->cache_.insert(path, new QPixmap(pix),
                                        qMax(1, int(pix.width() * pix.height() * 4 / 1024)));
                } else {
                    self->cache_.insert(path, new QPixmap, 1);
                }
                for (const int row : self->thumbnailRows_.value(path))
                    emit self->dataChanged(self->index(row), self->index(row),
                                           {Qt::DecorationRole});
                emit self->thumbnailReady();
            },
            Qt::QueuedConnection);
    });
    return {};
}
void GalleryDelegate::paint(QPainter *p, const QStyleOptionViewItem &o,
                            const QModelIndex &i) const {
    const auto *m = qobject_cast<const GalleryModel *>(i.model());
    if (!m)
        return;
    const auto photo = m->photo(i.row());
    const auto pix = m->thumbnail(i.row());
    const QRect r = o.rect.adjusted(1, 1, -11, -11);
    const bool selected = o.state & QStyle::State_Selected,
               hover = o.state & QStyle::State_MouseOver;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    p->setPen(QPen(QColor(selected ? QStringLiteral("#a8c7fa")
                          : hover  ? QStringLiteral("#53647c")
                                   : QStringLiteral("#253043")),
                   selected ? 1.5 : 1));
    p->setBrush(QColor(selected ? QStringLiteral("#1c2b43") : QStringLiteral("#151f2e")));
    p->drawRoundedRect(r, 12, 12);
    const QRect imageRect = r.adjusted(9, 9, -9, -58);
    QPainterPath clip;
    clip.addRoundedRect(imageRect, 7, 7);
    p->setClipPath(clip);
    static const QBrush checker = [] {
        QPixmap tile(16, 16);
        tile.fill(QColor(QStringLiteral("#131e2b")));
        QPainter pattern(&tile);
        pattern.fillRect(0, 0, 8, 8, QColor(QStringLiteral("#1d2a39")));
        pattern.fillRect(8, 8, 8, 8, QColor(QStringLiteral("#1d2a39")));
        return QBrush(tile);
    }();
    p->fillRect(imageRect, checker);
    if (!pix.isNull()) {
        QSizeF scaled = pix.size();
        scaled.scale(imageRect.size(), Qt::KeepAspectRatio);
        QRectF dest(QPointF(0, 0), scaled);
        dest.moveCenter(imageRect.center());
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        p->drawPixmap(dest, pix, QRectF(pix.rect()));
    } else {
        icon(QStringLiteral("image"), QColor(QStringLiteral("#35445b")))
            .paint(p, QRect(imageRect.center() - QPoint(14, 14), QSize(28, 28)));
    }
    p->setClipping(false);
    QRect heart(r.right() - 35, r.top() + 14, 24, 24);
    p->setPen(Qt::NoPen);
    p->setBrush(QColor(11, 17, 27, 210));
    p->drawEllipse(heart);
    icon(QStringLiteral("heart"),
         QColor(photo.favorite ? QStringLiteral("#b9d2fa") : QStringLiteral("#c2cede")))
        .paint(p, heart.adjusted(4, 4, -4, -4));
    QFont font = o.font;
    font.setPointSize(10);
    font.setWeight(QFont::DemiBold);
    p->setFont(font);
    p->setPen(QColor(QStringLiteral("#e7edf6")));
    p->drawText(QRect(r.left() + 12, r.bottom() - 47, r.width() - 24, 23), Qt::AlignVCenter,
                p->fontMetrics().elidedText(photo.name, Qt::ElideMiddle, r.width() - 24));
    font.setPointSize(9);
    font.setWeight(QFont::Normal);
    p->setFont(font);
    p->setPen(QColor(QStringLiteral("#8595ab")));
    p->drawText(QRect(r.left() + 12, r.bottom() - 24, r.width() - 24, 17), Qt::AlignVCenter,
                p->fontMetrics().elidedText(photo.matchReason.isEmpty()
                                                ? QStringLiteral("%1 × %2  ·  %3")
                                                      .arg(photo.width)
                                                      .arg(photo.height)
                                                      .arg(fileSize(photo.bytes))
                                                : photo.matchReason,
                                            Qt::ElideRight, r.width() - 24));
    p->restore();
}
bool GalleryDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                  const QStyleOptionViewItem &o, const QModelIndex &i) {
    if (event->type() == QEvent::MouseButtonRelease) {
        auto *e = static_cast<QMouseEvent *>(event);
        const QRect r = o.rect.adjusted(1, 1, -11, -11);
        if (QRect(r.right() - 39, r.top() + 10, 32, 32).contains(e->position().toPoint()) &&
            e->button() == Qt::LeftButton) {
            emit favoriteClicked(static_cast<GalleryModel *>(model)->photo(i.row()).id);
            return true;
        }
    }
    return false;
}
void EmptyArt::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(QStringLiteral("#304052")), 1));
    p.setBrush(QColor(QStringLiteral("#141f2d")));
    p.save();
    p.translate(100, 74);
    p.rotate(-12);
    p.drawRoundedRect(QRect(-70, -48, 125, 94), 12, 12);
    p.restore();
    p.save();
    p.translate(115, 75);
    p.rotate(8);
    p.setBrush(QColor(QStringLiteral("#1b2a43")));
    p.setPen(QColor(QStringLiteral("#627a9f")));
    p.drawRoundedRect(QRect(-63, -47, 126, 94), 12, 12);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(QStringLiteral("#a8c7fa")));
    p.drawEllipse(QRect(22, -28, 13, 13));
    p.setBrush(QColor(QStringLiteral("#496c9e")));
    p.drawPolygon(QPolygon{{-49, 32}, {-13, -17}, {12, 17}, {31, -5}, {51, 32}});
    p.restore();
}
} // namespace piclocate
