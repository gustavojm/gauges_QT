#include "worker.h"

#include <QBasicTimer>
#include <QThread>

#include <iostream>
#include <opencv2/core/types.hpp>

namespace {

uint32_t bgrToRgb(const cv::Scalar& c) {
    return (static_cast<uint32_t>(c[2]) << 16) |
           (static_cast<uint32_t>(c[1]) << 8)  |
           static_cast<uint32_t>(c[0]);
}

void drawDashedCircle(cv::Mat& img, cv::Point center, int radius,
                      cv::Scalar color, int thickness) {
    constexpr int kSegments = 20;
    constexpr double kDashAngle = 2.0 * kPi / kSegments;
    for (int i = 0; i < kSegments; i += 2) {
        double a0 = i * kDashAngle;
        double a1 = (i + 1) * kDashAngle;
        cv::ellipse(img, center, cv::Size(radius, radius), 0,
                    a0 * 180.0 / kPi, a1 * 180.0 / kPi,
                    color, thickness);
    }
}

void drawAllGauges(cv::Mat& img, const std::vector<CircularGauge>& detectors) {
    for (const auto& g : detectors)
        g.DrawOutline(img);
}

} // namespace

// ═══════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ═══════════════════════════════════════════════════════════════════

Worker::Worker(const std::string& videoPath, QObject* parent)
    : QObject(parent)
    , videoPath_(videoPath)
{
}

Worker::~Worker() {
    cap_.release();
}

// ═══════════════════════════════════════════════════════════════════
//  Helpers
// ═══════════════════════════════════════════════════════════════════

QImage Worker::matToQImage(const cv::Mat& bgr) {
    if (bgr.empty()) return {};
    cv::Mat rgb;
    cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
    QImage img(rgb.data, rgb.cols, rgb.rows, rgb.step, QImage::Format_RGB888);
    return img.copy();
}

// ═══════════════════════════════════════════════════════════════════
//  Initialization (runs on worker thread after moveToThread)
// ═══════════════════════════════════════════════════════════════════

void Worker::start() {
    cap_.open(videoPath_);
    if (!cap_.isOpened()) {
        std::cerr << "Worker: Could not open video\n";
        emit finished();
        return;
    }

    totalFrames_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_COUNT));
    fps_ = cap_.get(cv::CAP_PROP_FPS);

    if (!cap_.read(firstFrame_)) {
        std::cerr << "Worker: Could not read first frame\n";
        emit finished();
        return;
    }
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        std::cerr << "Warning: Could not reset to frame 0 in start()\n";

    det_.rois = CircularGauge::FindGauges(firstFrame_, det_.canny, det_.acc);
    displayDetectionOverlay();
    emit modeChanged(AppMode::kDetection);
}

// ═══════════════════════════════════════════════════════════════════
//  Detection Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::displayDetectionOverlay() {
    cv::Mat disp = firstFrame_.clone();
    const cv::Scalar color = {0, 0, 255};
    for (const auto& roi : det_.rois) {
        cv::circle(disp, roi.center, roi.radius, color, 2);
        cv::putText(disp, std::to_string(roi.radius),
                    roi.center + cv::Point(-20, 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    }
    emit frameReady(matToQImage(disp));
    emit detectionCountChanged(det_.rois.size());
}

void Worker::reRunDetection() {
    det_.rois = CircularGauge::FindGauges(firstFrame_, det_.canny, det_.acc);
    displayDetectionOverlay();
}

void Worker::setManualPlacement(bool enabled) {
    det_.manualPlacement = enabled;
    if (!enabled && mode_ == AppMode::kDetection)
        reRunDetection();
    emit manualPlacementActivated(enabled);
    emit manualInstructionChanged(enabled);
}

void Worker::setCanny(int value) {
    det_.canny = value;
    if (mode_ == AppMode::kDetection && !det_.manualPlacement)
        reRunDetection();
}

void Worker::setAcc(int value) {
    det_.acc = value;
    if (mode_ == AppMode::kDetection && !det_.manualPlacement)
        reRunDetection();
}

// ═══════════════════════════════════════════════════════════════════
//  Click Handling
// ═══════════════════════════════════════════════════════════════════

void Worker::onImageClicked(int x, int y) {
    if (quit_) return;

    if (mode_ == AppMode::kDetection && det_.manualPlacement)
        handleDetectionClick(x, y);
    else if (mode_ == AppMode::kCalibration && !circularGauges_.empty())
        handleCalibrationClick(x, y);
}

void Worker::handleDetectionClick(int x, int y) {
    if (!det_.manualPending) {
        det_.manualCenter = cv::Point(x, y);
        det_.manualPending = true;
    } else {
        int r = cvRound(cv::norm(cv::Point(x, y) - det_.manualCenter));
        det_.rois.push_back({det_.manualCenter, r});
        std::cout << "  >> Manual gauge at ("
                  << det_.manualCenter.x << ", " << det_.manualCenter.y
                  << "), radius=" << r << "\n";
        det_.manualPending = false;
    }

    cv::Mat disp = firstFrame_.clone();
    int label = 1;
    for (const auto& g : det_.rois) {
        cv::circle(disp, g.center, g.radius, cv::Scalar(0, 255, 0), 2);
        cv::putText(disp, std::to_string(label++),
                    g.center - cv::Point(8, 8),
                    cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
    }
    cv::circle(disp, det_.manualCenter, kManualCenterRadius,
               cv::Scalar(0, 255, 255), -1);
    drawDashedCircle(disp, det_.manualCenter, kManualGuideRadius,
                     cv::Scalar(0, 255, 255), 1);
    emit manualInstructionChanged(!det_.manualPending);
    emit frameReady(matToQImage(disp));
    emit detectionCountChanged(det_.rois.size());
}

void Worker::handleCalibrationClick(int x, int y) {
    // Hit-test markers on all gauges and start drag if a marker was hit
    for (size_t i = 0; i < circularGauges_.size(); i++) {
        int hit = circularGauges_[i].HandleClick(x, y);
        if (hit != CircularGauge::kMarkerNone) {
            cal_.draggingGaugeIdx = static_cast<int>(i);
            cal_.draggingMarker = hit;
            break;
        }
    }
    publishCalibrationDisplay();
}

// ═══════════════════════════════════════════════════════════════════
//  Mode Transitions
// ═══════════════════════════════════════════════════════════════════

void Worker::publishCalibrationDisplay() {
    cv::Mat disp;
    if (!calibFrame_.empty()) {
        disp = calibFrame_.clone();
    } else {
        disp = firstFrame_.clone();
    }

    drawAllGauges(disp, circularGauges_);

    for (const auto& d : circularGauges_)
        d.DrawCalibrationOverlay(disp);

    emit frameReady(matToQImage(disp));
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Data
// ═══════════════════════════════════════════════════════════════════

void Worker::refreshCalibData() {
    calibData_.resize(static_cast<int>(circularGauges_.size()));
    auto out = calibData_.begin();
    for (const auto& d : circularGauges_) {
        out->value = d.smoothedValue();
        out->minValue = d.scale().min_value;
        out->maxValue = d.scale().max_value;
        out->colorRgb = bgrToRgb(d.color());
        ++out;
    }
    emit calibrationDataReady(calibData_);
}

void Worker::updateGaugeValues() {
    for (int i = 0; i < calibData_.size(); ++i)
        calibData_[i].value = circularGauges_[i].smoothedValue();
    emit liveValuesUpdated(calibData_);
}

void Worker::enterCalibration() {
    mode_ = AppMode::kCalibration;
    emit modeChanged(AppMode::kCalibration);

    refreshCalibData();

    publishCalibrationDisplay();
}

void Worker::confirmGauges() {
    if (mode_ != AppMode::kDetection || det_.rois.empty())
        return;

    circularGauges_.clear();
    for (const auto& g : det_.rois) {
        circularGauges_.emplace_back(g.center, g.radius, CircularGauge::NextColor());
        std::cout << "  >> Gauge " << (circularGauges_.size() - 1)
                  << " at (" << g.center.x << ", " << g.center.y
                  << "), radius=" << g.radius << "\n";
    }
    det_.rois.clear();
    calibFrame_ = firstFrame_.clone();
    enterCalibration();
}

void Worker::confirmCalib() {
    if (mode_ != AppMode::kCalibration || circularGauges_.empty()) return;

    for (auto& d : circularGauges_) {
        d.FinalizeCalibration();
        const auto& s = d.scale();
        std::cout << "  >> Gauge scale: " << s.min_value << " at "
                  << (s.start_angle * 180.0 / kPi) << " deg, "
                  << s.max_value << " at "
                  << (s.end_angle * 180.0 / kPi) << " deg\n";
    }
    enterProcessing();
}

void Worker::setGaugeCalibRange(int idx, double minVal, double maxVal) {
    if (idx < 0 || idx >= static_cast<int>(circularGauges_.size())) return;
    auto& d = circularGauges_[idx];
    d.SetCalibrationValues(minVal, maxVal);
    if (mode_ == AppMode::kCalibration)
        publishCalibrationDisplay();
}

// ═══════════════════════════════════════════════════════════════════
//  Processing Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::enterProcessing() {
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        std::cerr << "Warning: Could not reset to frame 0 in enterProcessing()\n";

    frameCount_ = 0;

    mode_ = AppMode::kProcessing;
    emit modeChanged(AppMode::kProcessing);

    refreshCalibData();

    chainTimer_.start(0, this);
}

void Worker::processNextFrame() {
    chainTimer_.stop();
    if (quit_ || mode_ != AppMode::kProcessing) return;

    cv::Mat frame;
    if (!cap_.read(frame)) {
        std::cout << "End of video.\n";
        emit finished();
        return;
    }

    if (!frame.empty()) {
        int labelY = 60;
        for (auto& d : circularGauges_) {
            d.DetectNeedle(frame);
            d.DrawOverlay(frame, labelY);
            labelY += 30;
        }

        frameCount_++;

        updateGaugeValues();

        emit frameReady(matToQImage(frame));
        emit frameCountUpdated(frameCount_, totalFrames_);

        chainTimer_.start(0, this);
    }
}

void Worker::restart() {
    chainTimer_.stop();
    if (mode_ != AppMode::kProcessing) return;
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        std::cerr << "Warning: Could not reset to frame 0 in restart()\n";
    for (auto& d : circularGauges_) d.ResetSmoothing();
    frameCount_ = 0;
    chainTimer_.start(0, this);
}

// ═══════════════════════════════════════════════════════════════════
//  Timer
// ═══════════════════════════════════════════════════════════════════

void Worker::timerEvent(QTimerEvent* event) {
    if (event->timerId() == chainTimer_.timerId())
        processNextFrame();
}

// ═══════════════════════════════════════════════════════════════════
//  Drag Handlers
// ═══════════════════════════════════════════════════════════════════

void Worker::onDragMove(int x, int y) {
    if (cal_.draggingMarker == CircularGauge::kMarkerNone) return;
    if (mode_ != AppMode::kCalibration) return;
    if (cal_.draggingGaugeIdx < 0 ||
        static_cast<size_t>(cal_.draggingGaugeIdx) >= circularGauges_.size())
        return;

    auto& d = circularGauges_[static_cast<size_t>(cal_.draggingGaugeIdx)];
    d.MoveMarker(cal_.draggingMarker, cv::Point(x, y));
    publishCalibrationDisplay();
}

void Worker::onDragRelease() {
    cal_.draggingMarker = CircularGauge::kMarkerNone;
    cal_.draggingGaugeIdx = -1;
}

// ═══════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════

void Worker::quit() {
    quit_ = true;
    chainTimer_.stop();
    cap_.release();
    emit finished();
    QThread::currentThread()->quit();
}
