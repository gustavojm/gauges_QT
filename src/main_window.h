#pragma once

#include <QImage>
#include <QPixmap>
#include <QWidget>
#include <QString>

#include <string>

#include "gauge_detector.h"

class QCloseEvent;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QPixmap;
class QResizeEvent;
class QThread;

class Worker;
class DetectionPage;
class CalibrationPage;
class ProcessingPage;

// ─── Video Widget ─────────────────────────────────────────────────

class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    void setImage(const QImage& img);

signals:
    void imageClicked(int imageX, int imageY);
    void mouseMoved(int imageX, int imageY);
    void mouseReleased();

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

// ─── Main Window ──────────────────────────────────────────────────

class MainWindow : public QWidget {
    Q_OBJECT
public:
    explicit MainWindow(const std::string& videoPath);
    ~MainWindow() override;

signals:
    void quitRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    void setMode(AppMode mode);

    Worker* worker_ = nullptr;
    QThread* workerThread_ = nullptr;

    VideoWidget* videoWidget_;
    DetectionPage* detectionPage_;
    CalibrationPage* calibrationPage_;
    ProcessingPage* processingPage_;
    AppMode currentMode_ = AppMode::kDetection;
};
