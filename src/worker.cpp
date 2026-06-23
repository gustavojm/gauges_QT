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

void drawAllGauges(cv::Mat& img, const std::vector<std::unique_ptr<Gauge>>& detectors) {
    for (const auto& g : detectors)
        g->DrawOutline(img);
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

    det_.gauges.clear();
    for (const auto& roi : CircularGauge::FindGauges(firstFrame_, det_.canny, det_.acc)) {
        auto g = std::make_unique<CircularGauge>(roi.center, roi.radius,
                                                   Gauge::NextColor());
        if (!roi.H.empty())
            g->SetHomography(roi.H, roi.outSize, roi.center, roi.ellipse);
        det_.gauges.push_back(std::move(g));
    }
    for (const auto& roi : EdgewiseGauge::FindGauges(firstFrame_, det_.canny))
        det_.gauges.push_back(std::make_unique<EdgewiseGauge>(roi, Gauge::NextColor()));
    displayDetectionOverlay();
    emit modeChanged(AppMode::kDetection);
}

// ═══════════════════════════════════════════════════════════════════
//  Detection Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::displayDetectionOverlay() {
    cv::Mat disp = firstFrame_.clone();
    const cv::Scalar color = {0, 0, 255};
    for (const auto& g : det_.gauges) {
        g->DrawOutline(disp);
    }
    emit frameReady(matToQImage(disp));
    emit detectionCountChanged(det_.gauges.size());
}

void Worker::reRunDetection() {
    det_.gauges.clear();
    if (det_.activeType == GaugeType::kCircular) {
        for (const auto& roi : CircularGauge::FindGauges(firstFrame_, det_.canny, det_.acc)) {
            auto g = std::make_unique<CircularGauge>(roi.center, roi.radius,
                                                       Gauge::NextColor());
            if (!roi.H.empty())
                g->SetHomography(roi.H, roi.outSize, roi.center, roi.ellipse);
            det_.gauges.push_back(std::move(g));
        }
    } else {
        for (const auto& roi : EdgewiseGauge::FindGauges(firstFrame_, det_.canny))
            det_.gauges.push_back(
                std::make_unique<EdgewiseGauge>(roi, Gauge::NextColor()));
    }
    displayDetectionOverlay();
}

void Worker::setGaugeType(int typeIndex) {
    det_.activeType = static_cast<GaugeType>(typeIndex);
    if (mode_ == AppMode::kDetection && !det_.manualPlacement)
        reRunDetection();
}

void Worker::setManualPlacement(bool enabled) {
    det_.manualPlacement = enabled;
    if (enabled) {
        det_.manualStage = DetectionState::ManualStage::kEdge1;
        det_.manualEdges.clear();
        emit manualInstructionChanged(0);
    } else if (mode_ == AppMode::kDetection) {
        reRunDetection();
    }
    emit manualPlacementActivated(enabled);
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
    else if (mode_ == AppMode::kCalibration && !gauges_.empty())
        handleCalibrationClick(x, y);
}

void Worker::handleDetectionClick(int x, int y) {
    cv::Point click(x, y);

    det_.manualEdges.push_back(click);
    int n = static_cast<int>(det_.manualEdges.size());

    bool created = false;
    switch (det_.activeType) {
        case GaugeType::kEdgewise: {
            auto roi = EdgewiseGauge::FitFromManualEdges(det_.manualEdges);
            if (roi) {
                det_.gauges.push_back(
                    std::make_unique<EdgewiseGauge>(*roi, Gauge::NextColor()));
                created = true;
            }
            break;
        }
        case GaugeType::kCircular: {
            auto roi = CircularGauge::FitFromManualEdges(det_.manualEdges);
            if (roi && !roi->H.empty()) {
                auto g = std::make_unique<CircularGauge>(
                    roi->center, roi->radius, Gauge::NextColor());
                g->SetHomography(roi->H, roi->outSize, roi->center, roi->ellipse);
                det_.gauges.push_back(std::move(g));
                created = true;
            }
            break;
        }
    }

    if (created) {
        det_.manualEdges.clear();
        det_.manualStage = DetectionState::ManualStage::kEdge1;
        emit manualInstructionChanged(0);
    } else {
        emit manualInstructionChanged(n);
    }

    // Draw overlay
    cv::Mat disp = firstFrame_.clone();
    int label = 1;
    for (const auto& g : det_.gauges) {
        g->DrawOutline(disp);
        label++;
    }

    // Draw placed edge points and lines between them
    for (size_t i = 0; i < det_.manualEdges.size(); i++) {
        cv::circle(disp, det_.manualEdges[i], kManualCenterRadius,
                   cv::Scalar(0, 200, 255), -1);
        if (i > 0) {
            cv::line(disp, det_.manualEdges[i - 1], det_.manualEdges[i],
                     cv::Scalar(0, 200, 255), 1, cv::LINE_AA);
        }
    }
    // Close the polygon if 2+ points
    if (det_.manualEdges.size() >= 2) {
        cv::line(disp, det_.manualEdges.back(), det_.manualEdges.front(),
                 cv::Scalar(0, 200, 255), 1, cv::LINE_AA);
    }

    emit frameReady(matToQImage(disp));
    emit detectionCountChanged(det_.gauges.size());
}

void Worker::handleCalibrationClick(int x, int y) {
    // Hit-test markers on all gauges and start drag if a marker was hit
    for (size_t i = 0; i < gauges_.size(); i++) {
        int hit = gauges_[i]->HandleClick(x, y);
        if (hit != Gauge::kMarkerNone) {
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

    drawAllGauges(disp, gauges_);

    for (const auto& d : gauges_)
        d->DrawCalibrationOverlay(disp);

    emit frameReady(matToQImage(disp));
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Data
// ═══════════════════════════════════════════════════════════════════

void Worker::refreshCalibData() {
    calibData_.resize(static_cast<int>(gauges_.size()));
    auto out = calibData_.begin();
    for (const auto& d : gauges_) {
        out->value = d->smoothedValue();
        out->minValue = d->minValue();
        out->maxValue = d->maxValue();
        out->colorRgb = bgrToRgb(d->color());
        ++out;
    }
    emit calibrationDataReady(calibData_);
}

void Worker::updateGaugeValues() {
    for (int i = 0; i < calibData_.size(); ++i)
        calibData_[i].value = gauges_[i]->smoothedValue();
    emit liveValuesUpdated(calibData_);
}

void Worker::enterCalibration() {
    mode_ = AppMode::kCalibration;
    emit modeChanged(AppMode::kCalibration);

    refreshCalibData();

    publishCalibrationDisplay();
}

void Worker::confirmGauges() {
    if (mode_ != AppMode::kDetection || det_.gauges.empty())
        return;

    gauges_.clear();
    for (auto& g : det_.gauges) {
        std::cout << "  >> Gauge " << gauges_.size()
                  << " at (" << g->roi().center.x << ", "
                  << g->roi().center.y << ")"
                  << (dynamic_cast<CircularGauge*>(g.get()) &&
                      dynamic_cast<CircularGauge*>(g.get())->hasHomography()
                          ? " [homography]" : "")
                  << "\n";
        gauges_.push_back(std::move(g));
    }
    det_.gauges.clear();
    calibFrame_ = firstFrame_.clone();
    enterCalibration();
}

void Worker::confirmCalib() {
    if (mode_ != AppMode::kCalibration || gauges_.empty()) return;

    for (auto& d : gauges_) {
        d->FinalizeCalibration();
        // Log circular-specific scale info
        if (auto* cg = dynamic_cast<CircularGauge*>(d.get())) {
            const auto& s = cg->scale();
            std::cout << "  >> Gauge scale: " << s.min_value << " at "
                      << (s.start_angle * 180.0 / kPi) << " deg, "
                      << s.max_value << " at "
                      << (s.end_angle * 180.0 / kPi) << " deg\n";
        }
    }
    enterProcessing();
}

void Worker::setGaugeCalibRange(int idx, double minVal, double maxVal) {
    if (idx < 0 || idx >= static_cast<int>(gauges_.size())) return;
    auto& d = gauges_[idx];
    d->SetCalibrationValues(minVal, maxVal);
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
    motionInitialized_ = false;

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
        // Initialize motion features on the first processing frame
        // (skip for gauges that use homography-based rectification)
        if (!motionInitialized_) {
            for (auto& d : gauges_) {
                d->InitMotionFeatures(frame);
            }
            motionInitialized_ = true;
        }

        int labelY = 60;
        for (auto& d : gauges_) {
            d->UpdateROI(frame);
            d->DetectNeedle(frame);
            d->DrawOverlay(frame, labelY);
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
    for (auto& d : gauges_) {
        d->ResetMotionState();
        d->ResetSmoothing();
    }
    frameCount_ = 0;
    motionInitialized_ = false;
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
    if (cal_.draggingMarker == Gauge::kMarkerNone) return;
    if (mode_ != AppMode::kCalibration) return;
    if (cal_.draggingGaugeIdx < 0 ||
        static_cast<size_t>(cal_.draggingGaugeIdx) >= gauges_.size())
        return;

    auto& d = gauges_[static_cast<size_t>(cal_.draggingGaugeIdx)];
    d->MoveMarker(cal_.draggingMarker, cv::Point(x, y));
    publishCalibrationDisplay();
}

void Worker::onDragRelease() {
    cal_.draggingMarker = Gauge::kMarkerNone;
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
