#include "video_widget.h"

#include <QPainter>

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoWidget::setImage(const QImage& img) {
    image_ = img.copy();
    updateScaled();
    update();
}

void VideoWidget::updateScaled() {
    if (image_.isNull()) {
        scaled_ = QPixmap();
        return;
    }
    scaled_ = QPixmap::fromImage(image_).scaled(
        size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

void VideoWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(26, 26, 30));
    if (!scaled_.isNull()) {
        int x = (width() - scaled_.width()) / 2;
        int y = (height() - scaled_.height()) / 2;
        p.drawPixmap(x, y, scaled_);
    }
}

void VideoWidget::resizeEvent(QResizeEvent*) {
    updateScaled();
}

static bool WidgetToImage(const QImage& image, const QPixmap& scaled,
                          int w, int h, const QPoint& pos,
                          int& ix, int& iy) {
    if (image.isNull() || scaled.isNull()) return false;
    float sx = static_cast<float>(scaled.width()) / image.width();
    float sy = static_cast<float>(scaled.height()) / image.height();
    int ox = (w - scaled.width()) / 2;
    int oy = (h - scaled.height()) / 2;
    ix = static_cast<int>((pos.x() - ox) / sx);
    iy = static_cast<int>((pos.y() - oy) / sy);
    return ix >= 0 && ix < image.width() && iy >= 0 && iy < image.height();
}

void VideoWidget::mousePressEvent(QMouseEvent* event) {
    int ix, iy;
    if (WidgetToImage(image_, scaled_, width(), height(), event->pos(), ix, iy))
        emit imageClicked(ix, iy);
}

void VideoWidget::mouseMoveEvent(QMouseEvent* event) {
    if (!(event->buttons() & Qt::LeftButton)) return;
    int ix, iy;
    if (WidgetToImage(image_, scaled_, width(), height(), event->pos(), ix, iy))
        emit mouseMoved(ix, iy);
}

void VideoWidget::mouseReleaseEvent(QMouseEvent*) {
    emit imageMouseReleased();
}
