#include "worker.h"

#include <QBasicTimer>
#include <QThread>

#include <iostream>

namespace {

static uint32_t bgrToRgb(const cv::Scalar& c) {
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

void drawGaugeNumber(cv::Mat& img, cv::Point center, int number,
                     cv::Scalar color) {
    cv::putText(img, std::to_string(number),
                center - cv::Point(8, 8),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, color, 2);
}

void drawAllGauges(cv::Mat& img,
                   const std::vector<GaugeDetector>& detectors,
                   int highlightIdx = -1) {
    for (size_t i = 0; i < detectors.size(); i++) {
        const auto& d = detectors[i];
        if (d.gauge().radius <= 0) continue;
        cv::Scalar color = (static_cast<int>(i) == highlightIdx)
                               ? cv::Scalar(0, 255, 255)
                               : d.color();
        cv::circle(img, d.gauge().center, d.gauge().radius, color, 2);
        drawGaugeNumber(img, d.gauge().center, static_cast<int>(i + 1),
                        color);
    }
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
    if (writer_.isOpened()) writer_.release();
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

CalibUIState Worker::computeCalibUI() const {
    CalibUIState calib;
    if (detectors_.empty()) {
        calib.initialized = false;
        return calib;
    }
    if (currentGaugeIdx_ >= detectors_.size()) {
        calib.initialized = false;
        return calib;
    }
    const auto& d = detectors_[currentGaugeIdx_];
    calib.initialized = true;
    calib.state = d.state();
    calib.circleStage = d.circle_stage();
    calib.currentGauge = currentGaugeIdx_;
    calib.totalGauges = detectors_.size();
    return calib;
}

static const std::vector<cv::Scalar> kPalette = {
    {0, 0, 255},   {255, 0, 0},   {0, 255, 255},
    {255, 0, 255}, {255, 255, 0}, {0, 165, 255},
};

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

    detectedGauges_ = GaugeDetector::FindGauges(firstFrame_, canny_, acc_);
    displayDetectionOverlay();
    emit modeChanged(AppMode::kDetection);
}

// ═══════════════════════════════════════════════════════════════════
//  Detection Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::displayDetectionOverlay() {
    cv::Mat disp = firstFrame_.clone();
    for (size_t i = 0; i < detectedGauges_.size(); i++) {
        const auto& col = kPalette[i % kPalette.size()];
        cv::circle(disp, detectedGauges_[i].center,
                   detectedGauges_[i].radius, col, 2);
        cv::putText(disp, std::to_string(detectedGauges_[i].radius),
                    detectedGauges_[i].center + cv::Point(-20, 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, col, 1);
    }
    emit frameReady(matToQImage(disp));
    emit detectionUpdated(detectedGauges_.size());
}

void Worker::reRunDetection() {
    detectedGauges_ = GaugeDetector::FindGauges(firstFrame_, canny_, acc_);
    displayDetectionOverlay();
}

void Worker::setManualPlacement(bool enabled) {
    manualPlacement_ = enabled;
    if (!enabled && mode_ == AppMode::kDetection)
        reRunDetection();
    emit manualPlacementActive(enabled);
}

void Worker::setCanny(int value) {
    canny_ = value;
    if (mode_ == AppMode::kDetection && !manualPlacement_)
        reRunDetection();
}

void Worker::setAcc(int value) {
    acc_ = value;
    if (mode_ == AppMode::kDetection && !manualPlacement_)
        reRunDetection();
}

// ═══════════════════════════════════════════════════════════════════
//  Click Handling
// ═══════════════════════════════════════════════════════════════════

void Worker::handleClick(int x, int y) {
    if (quit_) return;

    if (mode_ == AppMode::kDetection && manualPlacement_) {
        if (!manualPending_) {
            manualCenter_ = cv::Point(x, y);
            manualPending_ = true;
        } else {
            int r = cvRound(cv::norm(cv::Point(x, y) - manualCenter_));
            detectedGauges_.push_back({manualCenter_, r});
            std::cout << "  >> Manual gauge at ("
                      << manualCenter_.x << ", " << manualCenter_.y
                      << "), radius=" << r << "\n";
            manualPending_ = false;
        }

        cv::Mat disp = firstFrame_.clone();
        for (size_t i = 0; i < detectedGauges_.size(); i++) {
            const auto& g = detectedGauges_[i];
            cv::circle(disp, g.center, g.radius, cv::Scalar(0, 255, 0), 2);
            drawGaugeNumber(disp, g.center, static_cast<int>(i + 1),
                            cv::Scalar(0, 255, 0));
        }
        cv::circle(disp, manualCenter_, kManualCenterRadius,
                   cv::Scalar(0, 255, 255), -1);
        drawDashedCircle(disp, manualCenter_, kManualGuideRadius,
                         cv::Scalar(0, 255, 255), 1);
        if (manualPending_) {
            cv::putText(disp, "Now click on the EDGE of the gauge face",
                        cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                        1.0, cv::Scalar(0, 255, 255), 2);
        } else {
            cv::putText(disp, "Click on the CENTER of the gauge",
                        cv::Point(10, 30), cv::FONT_HERSHEY_SIMPLEX,
                        1.0, cv::Scalar(0, 255, 255), 2);
        }
        emit frameReady(matToQImage(disp));
        emit detectionUpdated(detectedGauges_.size());

    } else if (mode_ == AppMode::kCalibration && !detectors_.empty() &&
               currentGaugeIdx_ < detectors_.size()) {
        auto& cur = detectors_[currentGaugeIdx_];
        if (cur.state() == GaugeState::kCircleManual) {
            cur.HandleClick(x, y);
            if (cur.circle_stage() == 3 && cur.circle_radius() > 0 &&
                cur.gauge().radius == 0) {
                cur.SetCircle(cur.circle_center(), cur.circle_radius());
                const auto& g = cur.gauge();
                std::cout << "  >> Gauge " << currentGaugeIdx_
                          << " at (" << g.center.x << ", " << g.center.y
                          << "), radius=" << g.radius << "\n";
                calibFrame_ = firstFrame_.clone();
            }
        }

        // Hit-test markers on all gauges (against inset positions)
        cv::Point click(x, y);
        for (size_t i = 0; i < detectors_.size(); i++) {
            auto& d = detectors_[i];
            if (d.state() != GaugeState::kCalibrating) continue;
            int thresh = std::max(d.gauge().radius / 6, 12);
            cv::Point center = d.gauge().center;
            cv::Point vecMin = d.pt_min() - center;
            cv::Point vecMax = d.pt_max() - center;
            cv::Point ptMinIn = center + cv::Point(
                cvRound(vecMin.x * kRadiusInset), cvRound(vecMin.y * kRadiusInset));
            cv::Point ptMaxIn = center + cv::Point(
                cvRound(vecMax.x * kRadiusInset), cvRound(vecMax.y * kRadiusInset));
            int dMin = cvRound(cv::norm(click - ptMinIn));
            int dMax = cvRound(cv::norm(click - ptMaxIn));
            int hit = GaugeDetector::kMarkerNone;
            if (dMin <= thresh && dMin <= dMax)
                hit = GaugeDetector::kMarkerMin;
            else if (dMax <= thresh)
                hit = GaugeDetector::kMarkerMax;
            if (hit != GaugeDetector::kMarkerNone) {
                currentGaugeIdx_ = i;
                draggingMarker_ = hit;
                d.MoveMarkerToPerimeter(hit, click,
                                        center, d.gauge().radius);
                break;
            }
        }

        publishCalibrationDisplay();
        emit calibUIUpdated(computeCalibUI());
    }
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

    if (detectors_.empty() || currentGaugeIdx_ >= detectors_.size()) {
        emit frameReady(matToQImage(disp));
        return;
    }

    const auto& cur = detectors_[currentGaugeIdx_];

    drawAllGauges(disp, detectors_, static_cast<int>(currentGaugeIdx_));

    // Circle manual placement
    if (cur.state() == GaugeState::kCircleManual) {
        if (cur.circle_stage() == 2) {
            cv::circle(disp, cur.circle_center(), kManualCenterRadius,
                       cv::Scalar(0, 255, 0), -1);
            drawDashedCircle(disp, cur.circle_center(), kManualGuideRadius,
                             cv::Scalar(0, 255, 0), 1);
        }
        emit frameReady(matToQImage(disp));
        return;
    }

    // Draw markers for all gauges that have them set
    for (size_t i = 0; i < detectors_.size(); i++) {
        const auto& d = detectors_[i];
        if (d.state() != GaugeState::kCalibrating) continue;
        if (d.pt_min() == cv::Point() && d.pt_max() == cv::Point()) continue;

        bool isCurrent = (static_cast<int>(i) == static_cast<int>(currentGaugeIdx_));
        int thickness = isCurrent ? 2 : 1;
        int arcR = cvRound(d.gauge().radius * kRadiusInset);

        cv::circle(disp, d.gauge().center, 4, cv::Scalar(255, 255, 255), -1);

        cv::Point vecMin = d.pt_min() - d.gauge().center;
        cv::Point vecMax = d.pt_max() - d.gauge().center;
        cv::Point ptMinIn = d.gauge().center + cv::Point(
            cvRound(vecMin.x * kRadiusInset), cvRound(vecMin.y * kRadiusInset));
        cv::Point ptMaxIn = d.gauge().center + cv::Point(
            cvRound(vecMax.x * kRadiusInset), cvRound(vecMax.y * kRadiusInset));

        double a0 = std::atan2(vecMin.y, vecMin.x) * 180.0 / kPi;
        double a1 = std::atan2(vecMax.y, vecMax.x) * 180.0 / kPi;
        if (a0 < 0) a0 += 360;
        if (a1 < 0) a1 += 360;
        {
            double cwEnd = (a1 > a0) ? a1 : a1 + 360;
            bool cwTop = (a0 <= 270 && 270 <= cwEnd);
            if (!cwTop) std::swap(a0, a1);
        }
        if (a1 <= a0) a1 += 360;
        cv::ellipse(disp, d.gauge().center,
                    cv::Size(arcR, arcR),
                    0, a0, a1,
                    cv::Scalar(0, 255, 255), thickness);

        cv::circle(disp, ptMinIn, 10, cv::Scalar(0, 255, 0), -1);
        cv::circle(disp, ptMinIn, 14, cv::Scalar(0, 255, 0), thickness);
        cv::putText(disp, "min", ptMinIn + cv::Point(12, 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        cv::circle(disp, ptMaxIn, 10, cv::Scalar(0, 0, 255), -1);
        cv::circle(disp, ptMaxIn, 14, cv::Scalar(0, 0, 255), thickness);
        cv::putText(disp, "max", ptMaxIn + cv::Point(12, 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);
    }

    emit frameReady(matToQImage(disp));
}

void Worker::enterCalibration() {
    mode_ = AppMode::kCalibration;
    emit modeChanged(AppMode::kCalibration);

    calibData_.resize(static_cast<int>(detectors_.size()));
    for (size_t i = 0; i < detectors_.size(); i++) {
        calibData_[i].value = 0;
        calibData_[i].minValue = detectors_[i].scale().min_value;
        calibData_[i].maxValue = detectors_[i].scale().max_value;
        calibData_[i].colorRgb = bgrToRgb(detectors_[i].color());
    }
    emit gaugeCalibUpdated(calibData_);

    publishCalibrationDisplay();
    emit calibUIUpdated(computeCalibUI());
}

void Worker::startCalibration() {
    if (mode_ != AppMode::kCalibration || detectors_.empty()) return;
    currentGaugeIdx_ = 0;
    calibFrame_ = firstFrame_.clone();
    calibData_.resize(static_cast<int>(detectors_.size()));
    for (size_t i = 0; i < detectors_.size(); i++) {
        calibData_[i].value = 0;
        calibData_[i].minValue = detectors_[i].scale().min_value;
        calibData_[i].maxValue = detectors_[i].scale().max_value;
        calibData_[i].colorRgb = bgrToRgb(detectors_[i].color());
    }
    emit gaugeCalibUpdated(calibData_);

    std::cout << "  >> Starting calibration for "
              << detectors_.size() << " gauge(s)\n";
    publishCalibrationDisplay();
    emit calibUIUpdated(computeCalibUI());
}

void Worker::confirmGauges() {
    if (mode_ != AppMode::kDetection || detectedGauges_.empty())
        return;

    detectors_.clear();
    for (const auto& g : detectedGauges_) {
        detectors_.emplace_back(g.center, g.radius, GaugeDetector::NextColor());
        std::cout << "  >> Gauge " << (detectors_.size() - 1)
                  << " at (" << g.center.x << ", " << g.center.y
                  << "), radius=" << g.radius << "\n";
    }
    detectedGauges_.clear();
    calibFrame_ = firstFrame_.clone();
    currentGaugeIdx_ = 0;
    enterCalibration();
}

void Worker::confirmCalib() {
    if (mode_ != AppMode::kCalibration || detectors_.empty()) return;

    for (size_t i = 0; i < detectors_.size(); i++) {
        auto& d = detectors_[i];
        d.CalibrateFromPoints(d.pt_min(), d.pt_max());
        d.SetCalibrationValid(true);
        const auto& s = d.scale();
        std::cout << "  >> Gauge " << i
                  << " scale: " << s.min_value << " at "
                  << (s.start_angle * 180.0 / kPi) << " deg, "
                  << s.max_value << " at "
                  << (s.end_angle * 180.0 / kPi) << " deg\n";
    }
    enterProcessing();
}

void Worker::setGaugeCalibRange(int idx, double minVal, double maxVal) {
    if (idx < 0 || idx >= static_cast<int>(detectors_.size())) return;
    auto& d = detectors_[idx];
    d.SetCalibrationValues(minVal, maxVal);
    d.SetCalibrationValid(true);
    if (mode_ == AppMode::kCalibration)
        publishCalibrationDisplay();
}

// ═══════════════════════════════════════════════════════════════════
//  Processing Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::enterProcessing() {
    std::string outputPath =
        videoPath_.substr(0, videoPath_.find_last_of('.')) + "_output.avi";
    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    if (!writer_.open(outputPath, fourcc, fps_, firstFrame_.size()))
        std::cerr << "Warning: Could not open output video\n";
    else
        std::cout << "  >> Output: " << outputPath << "\n";

    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        std::cerr << "Warning: Could not reset to frame 0 in enterProcessing()\n";
    for (auto& d : detectors_)
        d.StartProcessing();
    frameCount_ = 0;

    mode_ = AppMode::kProcessing;
    emit modeChanged(AppMode::kProcessing);

    calibData_.resize(static_cast<int>(detectors_.size()));
    for (size_t i = 0; i < detectors_.size(); i++) {
        auto& d = detectors_[i];
        calibData_[i].value = d.GetSmoothedValue();
        calibData_[i].minValue = d.scale().min_value;
        calibData_[i].maxValue = d.scale().max_value;
        calibData_[i].colorRgb = bgrToRgb(d.color());
    }
    emit gaugeCalibUpdated(calibData_);
   
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

    int labelY = 60;
    for (size_t i = 0; i < detectors_.size(); i++) {
        auto& d = detectors_[i];
        d.DetectNeedle(frame);
        d.DrawOverlay(frame, labelY);
        drawGaugeNumber(frame, d.gauge().center,
                        static_cast<int>(i + 1), d.color());
        labelY += 30;
    }

    if (writer_.isOpened()) writer_.write(frame);
    frameCount_++;

    calibData_.resize(static_cast<int>(detectors_.size()));
    for (size_t i = 0; i < detectors_.size(); i++) {
        calibData_[i].value = detectors_[i].GetSmoothedValue();
        calibData_[i].minValue = detectors_[i].scale().min_value;
        calibData_[i].maxValue = detectors_[i].scale().max_value;
        calibData_[i].colorRgb = bgrToRgb(detectors_[i].color());
    }

    emit frameReady(matToQImage(frame));
    emit gaugeCalibUpdated(calibData_);
    emit frameCountUpdated(frameCount_, totalFrames_);

    chainTimer_.start(0, this);
}

void Worker::restart() {
    chainTimer_.stop();
    if (mode_ != AppMode::kProcessing) return;
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        std::cerr << "Warning: Could not reset to frame 0 in restart()\n";
    for (auto& d : detectors_) d.ResetSmoothing();
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

void Worker::handleDragMove(int x, int y) {
    if (draggingMarker_ == GaugeDetector::kMarkerNone) return;
    if (mode_ != AppMode::kCalibration || detectors_.empty()) return;
    if (currentGaugeIdx_ >= detectors_.size()) return;

    auto& d = detectors_[currentGaugeIdx_];
    if (d.state() != GaugeState::kCalibrating) return;

    d.MoveMarkerToPerimeter(draggingMarker_, cv::Point(x, y),
                            d.gauge().center, d.gauge().radius);
    publishCalibrationDisplay();
}

void Worker::handleDragRelease() {
    draggingMarker_ = GaugeDetector::kMarkerNone;
}

// ═══════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════

void Worker::quit() {
    chainTimer_.stop();
    quit_ = true;
    cap_.release();
    if (writer_.isOpened()) writer_.release();
    emit finished();
    QThread::currentThread()->quit();
}
