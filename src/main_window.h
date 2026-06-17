#pragma once

#include <QCloseEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QThread>
#include <QWidget>

#include <string>

#include "gauge_detector.h"
#include "worker.h"
#include "detection_page.h"
#include "calibration_page.h"
#include "processing_page.h"

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
