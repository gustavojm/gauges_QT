#pragma once

#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QWidget>

class VideoWidget : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(VideoWidget)

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setImage(const QImage& img);

signals:
    void imageClicked(int imageX, int imageY);
    void mouseMoved(int imageX, int imageY);
    void imageMouseReleased();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage image_;
    QPixmap scaled_;
    void updateScaled();
};
