#include "worker.h"
#include "gauge.h"

#include <QDebug>
#include <QBasicTimer>
#include <QThread>

#include <opencv2/core/types.hpp>

namespace {

uint32_t BgrToRgb(const cv::Scalar& c) {
    return (static_cast<uint32_t>(c[2]) << 16) |
           (static_cast<uint32_t>(c[1]) << 8)  |
           static_cast<uint32_t>(c[0]);
}

void DrawAllGauges(cv::Mat& img, const std::vector<std::unique_ptr<Gauge>>& detectors) {
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

QImage Worker::MatToQImage(const cv::Mat& bgr) {
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
        qCritical() << "Worker: Could not open video";
        emit finished();
        return;
    }

    totalFrames_ = static_cast<int>(cap_.get(cv::CAP_PROP_FRAME_COUNT));
    fps_ = cap_.get(cv::CAP_PROP_FPS);

    if (!cap_.read(firstFrame_)) {
        qCritical() << "Worker: Could not read first frame";
        emit finished();
        return;
    }
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        qWarning() << "Could not reset to frame 0 in start()";

    det_.gauges.clear();
    DetectGauges();
    DisplayDetectionOverlay();
    emit modeChanged(AppMode::kDetection);
}

// ═══════════════════════════════════════════════════════════════════
//  Detection Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::DisplayDetectionOverlay() {
    cv::Mat disp = firstFrame_.clone();
    for (const auto& g : det_.gauges) {
        g->DrawOutline(disp);
    }
    emit frameReady(MatToQImage(disp));
    emit detectionCountChanged(static_cast<int>(det_.gauges.size()));
}

void Worker::DetectGauges(bool onlyActiveType) {
    if (!onlyActiveType || det_.activeType == GaugeType::kCircular) {
        for (const auto& roi : CircularGauge::FindGauges(firstFrame_, det_.canny, 40)) {
            auto g = std::make_unique<CircularGauge>(roi.center, roi.radius,
                                                       Gauge::NextColor());
            if (!roi.H.empty())
                g->SetHomography(roi.H, roi.outSize, roi.center, roi.ellipse);
            det_.gauges.push_back(std::move(g));
        }
    }
    if (!onlyActiveType || det_.activeType == GaugeType::kEdgewise) {
        for (const auto& roi : EdgewiseGauge::FindGauges(firstFrame_, det_.canny))
            det_.gauges.push_back(std::make_unique<EdgewiseGauge>(roi, Gauge::NextColor()));
    }
}

void Worker::ReRunDetection() {
    std::vector<std::unique_ptr<Gauge>> preserved;
    for (auto& g : det_.gauges) {
        if (g->is_manual()) {
            preserved.push_back(std::move(g));
        } else if ((det_.activeType == GaugeType::kCircular &&
                    !dynamic_cast<CircularGauge*>(g.get())) ||
                   (det_.activeType == GaugeType::kEdgewise &&
                    !dynamic_cast<EdgewiseGauge*>(g.get()))) {
            preserved.push_back(std::move(g));
        }
    }
    det_.gauges.clear();

    DetectGauges(true);

    for (auto& g : preserved)
        det_.gauges.push_back(std::move(g));
   
    DisplayDetectionOverlay();
}

void Worker::setGaugeType(GaugeType type) {
    det_.activeType = type;
    if (mode_ == AppMode::kDetection && !det_.manualPlacement)
        ReRunDetection();
}

void Worker::setManualPlacement(bool enabled) {
    det_.manualPlacement = enabled;
    if (enabled) {
        det_.manualEdges.clear();
        emit manualInstructionChanged(0);
    }
    emit manualPlacementActivated(enabled);
    emit detectionCountChanged(static_cast<int>(det_.gauges.size()));
}

void Worker::setCanny(int value) {
    det_.canny = value;
    if (mode_ == AppMode::kDetection && !det_.manualPlacement)
        ReRunDetection();
}

// ═══════════════════════════════════════════════════════════════════
//  Click Handling
// ═══════════════════════════════════════════════════════════════════

void Worker::onImageClicked(int x, int y) {
    if (quit_) return;

    if (mode_ == AppMode::kDetection && det_.manualPlacement)
        HandleDetectionClick(x, y);
    else if (mode_ == AppMode::kCalibration && !gauges_.empty())
        HandleCalibrationClick(x, y);
}

void Worker::HandleDetectionClick(int x, int y) {
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
                det_.gauges.back()->set_manual(true);
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
                g->set_manual(true);
                det_.gauges.push_back(std::move(g));
                created = true;
            }
            break;
        }
    }

    if (created) {
        det_.manualEdges.clear();
        emit manualInstructionChanged(0);
    } else {
        emit manualInstructionChanged(n);
    }

    // Draw overlay
    cv::Mat disp = firstFrame_.clone();
    for (const auto& g : det_.gauges) {
        g->DrawOutline(disp);
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

    emit frameReady(MatToQImage(disp));
    emit detectionCountChanged(static_cast<int>(det_.gauges.size()));
}

void Worker::HandleCalibrationClick(int x, int y) {
    // Hit-test markers on all gauges and start drag if a marker was hit
    for (size_t i = 0; i < gauges_.size(); i++) {
        CalibrationMarker hit = gauges_[i]->HandleClick(x, y);
        if (hit != CalibrationMarker::kNone) {
            cal_.draggingGaugeIdx = static_cast<int>(i);
            cal_.draggingMarker = hit;
            break;
        }
    }
    PublishCalibrationDisplay();
}

// ═══════════════════════════════════════════════════════════════════
//  Mode Transitions
// ═══════════════════════════════════════════════════════════════════

void Worker::PublishCalibrationDisplay() {
    cv::Mat disp;
    if (!calibFrame_.empty()) {
        disp = calibFrame_.clone();
    } else {
        disp = firstFrame_.clone();
    }

    DrawAllGauges(disp, gauges_);

    for (const auto& d : gauges_)
        d->DrawCalibrationOverlay(disp);

    emit frameReady(MatToQImage(disp));
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Data
// ═══════════════════════════════════════════════════════════════════

void Worker::RefreshCalibData() {
    calibData_.resize(static_cast<int>(gauges_.size()));
    auto out = calibData_.begin();
    for (const auto& d : gauges_) {
        out->value = d->smoothed_value();
        out->min_value = d->min_value();
        out->max_value = d->max_value();
        out->color_rgb = BgrToRgb(d->color());
        out->tag = QString::fromStdString(d->tag());
        ++out;
    }
    emit calibrationDataReady(calibData_);
}

void Worker::UpdateGaugeValues() {
    for (qsizetype i = 0; i < calibData_.size(); ++i) {
        calibData_[i].value = gauges_[i]->smoothed_value();
        calibData_[i].alarm_enabled = gauges_[i]->alarm_enabled();
        calibData_[i].alarm_direction = gauges_[i]->alarm_direction();
        calibData_[i].alarm_threshold = gauges_[i]->alarm_threshold();

        bool triggered = gauges_[i]->CheckAlarm();
        if (calibData_[i].alarm_triggered != triggered) {
            calibData_[i].alarm_triggered = triggered;
            emit alarmTriggered(static_cast<int>(i), triggered);
        }
    }
    emit liveValuesUpdated(calibData_);
}

void Worker::EnterCalibration() {
    mode_ = AppMode::kCalibration;
    emit modeChanged(AppMode::kCalibration);

    RefreshCalibData();

    PublishCalibrationDisplay();
}

void Worker::confirmGauges() {
    if (mode_ != AppMode::kDetection || det_.gauges.empty())
        return;

    gauges_.clear();

    std::sort(det_.gauges.begin(), det_.gauges.end(), [](const auto& a, const auto& b) {
        if (a->roi().center.y != b->roi().center.y)
            return a->roi().center.y < b->roi().center.y;   // top to bottom

        return a->roi().center.x < b->roi().center.x;       // left to right
    });

    for (auto& g : det_.gauges) {
        int num = static_cast<int>(gauges_.size()) + 1;
        qDebug() << "Gauge" << num
                  << "at (" << g->roi().center.x << ","
                  << g->roi().center.y << ")"
                  << (dynamic_cast<CircularGauge*>(g.get()) &&
                      dynamic_cast<CircularGauge*>(g.get())->has_homography()
                          ? " [homography]" : "");

        g->set_number(num);
        gauges_.push_back(std::move(g));
    }
    det_.gauges.clear();
    calibFrame_ = firstFrame_.clone();
    EnterCalibration();
}

void Worker::confirmCalib() {
    if (mode_ != AppMode::kCalibration || gauges_.empty()) return;

    for (const auto& d : gauges_) {
        d->FinalizeCalibration();
        // Log circular-specific scale info
        if (auto* cg = dynamic_cast<CircularGauge*>(d.get())) {
            const auto& s = cg->scale();
            qDebug() << "Gauge scale:" << s.min_value << "at"
                      << (s.start_angle * 180.0 / kPi) << "deg,"
                      << s.max_value << "at"
                      << (s.end_angle * 180.0 / kPi) << "deg";
        }
    }
    EnterProcessing();
}

void Worker::setGaugeCalibRange(int idx, double minVal, double maxVal) {
    if (idx < 0 || idx >= static_cast<int>(gauges_.size())) return;
    auto& d = gauges_[idx];
    d->SetCalibrationValues(minVal, maxVal);
    if (mode_ == AppMode::kCalibration)
        PublishCalibrationDisplay();
}

void Worker::setAlarmEnabled(int idx, bool enabled) {
    if (idx < 0 || idx >= static_cast<int>(gauges_.size())) return;
    gauges_[idx]->set_alarm_enabled(enabled);
}

void Worker::setAlarmDirection(int idx, AlarmDirection direction) {
    if (idx < 0 || idx >= static_cast<int>(gauges_.size())) return;
    gauges_[idx]->set_alarm_direction(direction);
}

void Worker::setAlarmThreshold(int idx, double threshold) {
    if (idx < 0 || idx >= static_cast<int>(gauges_.size())) return;
    gauges_[idx]->set_alarm_threshold(threshold);
}

void Worker::setTag(int idx, const QString& tag) {
    if (idx < 0 || idx >= static_cast<int>(gauges_.size())) return;
    gauges_[idx]->set_tag(tag.toStdString());
    if (idx < calibData_.size())
        calibData_[idx].tag = tag;
}

// ═══════════════════════════════════════════════════════════════════
//  Processing Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::EnterProcessing() {
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        qWarning() << "Could not reset to frame 0 in EnterProcessing()";

    frameCount_ = 0;
    motionInitialized_ = false;

    mode_ = AppMode::kProcessing;
    emit modeChanged(AppMode::kProcessing);

    RefreshCalibData();

    chainTimer_.start(0, this);
}

void Worker::ProcessNextFrame() {
    chainTimer_.stop();
    if (quit_ || mode_ != AppMode::kProcessing) return;

    cv::Mat frame;
    if (!cap_.read(frame)) {
        qDebug() << "End of video";
        emit finished();
        return;
    }

    if (!frame.empty()) {
        // Initialize motion features on the first processing frame
        // (skip for gauges that use homography-based rectification)
        if (!motionInitialized_) {
            for (const auto& d : gauges_) {
                d->InitMotionFeatures(frame);
            }
            motionInitialized_ = true;
        }

        int labelY = 60;
        for (const auto& d : gauges_) {
            d->UpdateROI(frame);
            d->DetectNeedle(frame);
            d->DrawOverlay(frame, labelY);
            labelY += 30;
        }

        frameCount_++;

        UpdateGaugeValues();

        emit frameReady(MatToQImage(frame));
        emit frameCountUpdated(frameCount_, totalFrames_);

        chainTimer_.start(0, this);
    }
}

void Worker::restart() {
    chainTimer_.stop();
    if (mode_ != AppMode::kProcessing) return;
    if (!cap_.set(cv::CAP_PROP_POS_FRAMES, 0))
        qWarning() << "Could not reset to frame 0 in restart()";
    for (const auto& d : gauges_) {
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
        ProcessNextFrame();
}

// ═══════════════════════════════════════════════════════════════════
//  Drag Handlers
// ═══════════════════════════════════════════════════════════════════

void Worker::onDragMove(int x, int y) {
    if (cal_.draggingMarker == CalibrationMarker::kNone) return;
    if (mode_ != AppMode::kCalibration) return;
    if (cal_.draggingGaugeIdx < 0 ||
        static_cast<size_t>(cal_.draggingGaugeIdx) >= gauges_.size())
        return;

    auto& d = gauges_[static_cast<size_t>(cal_.draggingGaugeIdx)];
    d->MoveMarker(cal_.draggingMarker, cv::Point(x, y));
    PublishCalibrationDisplay();
}

void Worker::onDragRelease() {
    cal_.draggingMarker = CalibrationMarker::kNone;
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
