#pragma once

#include <QBasicTimer>
#include <QImage>
#include <QObject>
#include <QTimerEvent>
#include <QVector>

#include <opencv2/opencv.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "circular_gauge.h"
#include "edgewise_gauge.h"

struct GaugeCalibData {
    std::optional<double> value = std::nullopt;
    double minValue = 0;
    double maxValue = 1000;
    uint32_t colorRgb = 0x00FF00;
};

Q_DECLARE_METATYPE(GaugeCalibData)

struct DetectionState {
    std::vector<CircularGauge::ROI> rois;
    std::vector<cv::Rect> edgewiseRois;
    GaugeType activeType = GaugeType::kCircular;
    bool manualPlacement = false;
    int canny = 320;
    int acc = 40;

    // 5-click manual placement state (perimeter points only)
    enum class ManualStage { kEdge1, kEdge2, kEdge3, kEdge4, kEdge5 };
    ManualStage manualStage = ManualStage::kEdge1;
    std::vector<cv::Point> manualEdges;

    // Per-gauge homography data (parallels rois vector)
    struct HomographyData {
        cv::Mat H;
        cv::Size outSize;
        cv::Point center;       // inferred from the conic fit
        cv::RotatedRect ellipseRect;
    };
    std::vector<HomographyData> homographies;
};

struct CalibrationState {
    int draggingGaugeIdx = -1;
    int draggingMarker = Gauge::kMarkerNone;
};

class Worker : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Worker)

public:
    explicit Worker(const std::string& videoPath, QObject* parent = nullptr);
    ~Worker() override;

signals:
    void frameReady(const QImage& image);
    void calibrationDataReady(const QVector<GaugeCalibData>& calib);
    void liveValuesUpdated(const QVector<GaugeCalibData>& calib);
    void frameCountUpdated(int current, int total);
    void detectionCountChanged(int numGauges);
    void modeChanged(AppMode mode);
    void manualPlacementActivated(bool active);
    void manualInstructionChanged(int stage);
    void gaugeTypeChanged(int typeIndex);
    void finished();

public slots:
    void start();
    void onImageClicked(int x, int y);
    void setManualPlacement(bool enabled);
    void setGaugeType(int typeIndex);
    void setCanny(int value);
    void setAcc(int value);
    void confirmGauges();
    void confirmCalib();
    void setGaugeCalibRange(int idx, double minVal, double maxVal);
    void onDragMove(int x, int y);
    void onDragRelease();
    void restart();
    void quit();

private:
    void timerEvent(QTimerEvent* event) override;
    void processNextFrame();

    // Mode-specific click handlers
    void handleDetectionClick(int x, int y);
    void handleCalibrationClick(int x, int y);

    static QImage matToQImage(const cv::Mat& bgr);
    void displayDetectionOverlay();
    void reRunDetection();
    void publishCalibrationDisplay();
    void enterCalibration();
    void enterProcessing();
    void refreshCalibData();
    void updateGaugeValues();

    std::string videoPath_;
    cv::VideoCapture cap_;
    int totalFrames_ = 0;
    double fps_ = 0;

    cv::Mat firstFrame_;
    cv::Mat calibFrame_;
    std::vector<std::unique_ptr<Gauge>> circularGauges_;

    DetectionState det_;
    CalibrationState cal_;

    int frameCount_ = 0;
    AppMode mode_ = AppMode::kDetection;

    QBasicTimer chainTimer_;
    QVector<GaugeCalibData> calibData_;

    // Safe: only set via the queued quit() slot from the main thread.
    bool quit_ = false;

    // Motion compensation: initialized on first processing frame
    bool motionInitialized_ = false;
};
