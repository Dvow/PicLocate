#pragma once
#include "core/types.h"
#include <QtWidgets>

namespace piclocate {
class ImageCanvas : public QGraphicsView {
  public:
    explicit ImageCanvas(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void fit();
    void zoom(double factor);

  protected:
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

  private:
    QGraphicsScene scene_;
    bool fitted_ = true;
};
class ImageViewer : public QDialog {
  public:
    ImageViewer(Photos photos, qint64 selectedId, QWidget *parent = nullptr);

  private:
    Photos photos_;
    int index_ = 0;
    quint64 generation_ = 0;
    QFutureWatcher<QImage> *watcher_ = nullptr;
    ImageCanvas *canvas_;
    QLabel *caption_, *position_;
    QPushButton *previous_, *next_;
    void showIndex(int index);
};
} // namespace piclocate
