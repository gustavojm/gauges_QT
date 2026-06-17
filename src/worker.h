#pragma once

#include <QImage>
#include <QObject>
#include <QVector>

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

#include "gauge_detector.h"

class QTimer;

class Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(const std::string& videoPath, QObject* parent = nullptr);
    ~Worker() override;

signals:
    void frameReady(const QImage& image);
    void gaugeValuesUpdated(const QVector<double>& values);
    void frameCountUpdated(int current, int total);
    void detectionUpdated(size_t numGauges);
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
    void setCalibMin(int value);
    void setCalibMax(int value);
    void startCalibration();
    void handleDragMove(int x, int y);
    void handleDragRelease();
    void restart();
    void quit();

private slots:
    void processNextFrame();

private:
    CalibUIState computeCalibUI() const;
    QImage matToQImage(const cv::Mat& bgr);
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
    int prevCanny_ = -1;
    int prevAcc_ = -1;
    bool manualPending_ = false;
    cv::Point manualCenter_;
    cv::VideoWriter writer_;

    int canny_ = 320;
    int acc_ = 40;
    bool manualPlacement_ = false;
    int calibMinVal_ = 0;
    int calibMaxVal_ = 1000;
    int frameCount_ = 0;

    AppMode mode_ = AppMode::kDetection;
    bool quit_ = false;
    int draggingMarker_ = GaugeDetector::kMarkerNone;

    QTimer* processTimer_ = nullptr;
};
