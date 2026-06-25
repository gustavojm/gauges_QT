#pragma once

#include <QImage>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QWidget>

/**
 * @class VideoWidget
 * @brief Custom QWidget that displays a QImage with aspect-ratio-preserving scaling.
 *
 * Handles mouse click, drag, and release events, translating widget
 * coordinates back to image coordinates and emitting signals.
 *
 * @see MainWindow
 * @see Worker
 */
class VideoWidget : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(VideoWidget)

public:
    /**
     * @brief Constructs the video widget.
     * @param parent  Qt parent widget.
     */
    explicit VideoWidget(QWidget* parent = nullptr);

    /**
     * @brief Sets the image to display.
     * @param img  QImage in RGB888 format.
     */
    void setImage(const QImage& img);

signals:
    /** @signal Emitted when the user clicks on the image. */
    void imageClicked(int imageX, int imageY);

    /** @signal Emitted when the user drags the mouse over the image. */
    void mouseMoved(int imageX, int imageY);

    /** @signal Emitted when the user releases the mouse button. */
    void imageMouseReleased();

protected:
    /**
     * @brief Paints the scaled image onto the widget.
     * @param event  Paint event.
     */
    void paintEvent(QPaintEvent* event) override;

    /**
     * @brief Handles mouse press — translates to image coordinates and emits imageClicked.
     * @param event  Mouse event.
     */
    void mousePressEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse move — translates to image coordinates and emits mouseMoved.
     * @param event  Mouse event.
     */
    void mouseMoveEvent(QMouseEvent* event) override;

    /**
     * @brief Handles mouse release — emits imageMouseReleased.
     * @param event  Mouse event.
     */
    void mouseReleaseEvent(QMouseEvent* event) override;

    /**
     * @brief Handles widget resize — updates the cached scaled pixmap.
     * @param event  Resize event.
     */
    void resizeEvent(QResizeEvent* event) override;

private:
    QImage image_;     ///< Current source image.
    QPixmap scaled_;   ///< Cached scaled pixmap for fast painting.

    /**
     * @brief Rebuilds the cached scaled pixmap from the current image and widget size.
     */
    void updateScaled();
};
