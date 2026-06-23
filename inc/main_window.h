#pragma once

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMainWindow>

#include <string>

#include "circular_gauge.h"
#include "worker.h"
#include "video_widget.h"
#include "detection_page.h"
#include "calibration_page.h"
#include "processing_page.h"

class MainWindow : public QMainWindow {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MainWindow)

public:
    explicit MainWindow(const std::string& videoPath);
    ~MainWindow() override;

signals:
    void quitRequested();

public slots:
    void setStatus(const QString& message);

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
