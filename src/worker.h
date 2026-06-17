#pragma once

#include <QBasicTimer>
#include <QImage>
#include <QObject>
#include <QTimerEvent>
#include <QVector>

#include <opencv2/opencv.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "gauge_detector.h"

struct GaugeCalibData {
    double value = 0;
    double minValue = 0;
    double maxValue = 1000;
    uint32_t colorRgb = 0x00FF00;
};

Q_DECLARE_METATYPE(GaugeCalibData)

class Worker : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Worker)
    
public:
    explicit Worker(const std::string& videoPath, QObject* parent = nullptr);
    ~Worker() override;

signals:
    void frameReady(const QImage& image);
    void gaugeCalibUpdated(const QVector<GaugeCalibData>& calib);
    void frameCountUpdated(int current, int total);
    void detectionUpdated(int numGauges);
    void calibUIUpdated(const CalibUIState& state);
    void modeChanged(AppMode mode);
    void manualPlacementActive(bool active);
    void finished();

public slots:
    void start();
    void handleClick(int x, int y);
    void setManualPlacement(bool enabled);
    void setCanny(int value);
    void setAcc(int value);
    void confirmGauges();
    void confirmCalib();
    void startCalibration();
    void setGaugeCalibRange(int idx, double minVal, double maxVal);
    void handleDragMove(int x, int y);
    void handleDragRelease();
    void restart();
    void quit();

private slots:
    void processNextFrame();

private:
    void timerEvent(QTimerEvent* event) override;

    CalibUIState computeCalibUI() const;
    static QImage matToQImage(const cv::Mat& bgr);
    void displayDetectionOverlay();
    void reRunDetection();
    void publishCalibrationDisplay();
    void enterCalibration();
    void enterProcessing();

    std::string videoPath_;
    cv::VideoCapture cap_;
    int totalFrames_ = 0;
    double fps_ = 0;

    cv::Mat firstFrame_;
    cv::Mat calibFrame_;
    std::vector<GaugeDetector> detectors_;
    std::vector<GaugeROI> detectedGauges_;
    size_t currentGaugeIdx_ = 0;
    bool manualPending_ = false;
    cv::Point manualCenter_;
    cv::VideoWriter writer_;

    int canny_ = 320;
    int acc_ = 40;
    bool manualPlacement_ = false;
    int frameCount_ = 0;

    AppMode mode_ = AppMode::kDetection;
    bool quit_ = false;
    int draggingMarker_ = GaugeDetector::kMarkerNone;

    QBasicTimer chainTimer_;
    QVector<GaugeCalibData> calibData_;

};
