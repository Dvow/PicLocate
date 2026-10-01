#pragma once
#include "core/types.h"
#include <QtWidgets>
namespace piclocate {
QIcon icon(const QString &name, const QColor &color = QColor(QStringLiteral("#a9b7c9")));
QSize galleryCardSize(int viewportWidth, int preferredWidth);
class GalleryModel : public QAbstractListModel {
    Q_OBJECT
  public:
    explicit GalleryModel(QObject *parent = nullptr, Paths paths = {});
    ~GalleryModel() override;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    void setPhotos(Photos photos);
    void appendPhotos(const Photos &photos);
    const Photos &photos() const { return photos_; }
    Photo photo(int row) const;
    void updatePhoto(const Photo &photo);
    void setCardSize(int width);
    int cardWidth() const { return cardWidth_; }
    QPixmap thumbnail(int row) const;
  signals:
    void thumbnailReady();

  private:
    Photos photos_;
    Paths paths_;
    int cardWidth_ = 210;
    QHash<QString, QList<int>> thumbnailRows_;
    mutable QCache<QString, QPixmap> cache_{64 * 1024};
    mutable QSet<QString> pending_;
    mutable QThreadPool pool_;
};
class GalleryDelegate : public QStyledItemDelegate {
    Q_OBJECT
  public:
    explicit GalleryDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    void paint(QPainter *p, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model, const QStyleOptionViewItem &option,
                     const QModelIndex &index) override;
  signals:
    void favoriteClicked(qint64 id);
};
class EmptyArt : public QWidget {
  public:
    explicit EmptyArt(QWidget *parent = nullptr) : QWidget(parent) { setFixedSize(210, 155); }

  protected:
    void paintEvent(QPaintEvent *) override;
};
} // namespace piclocate
