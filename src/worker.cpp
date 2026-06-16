#include "worker.h"

#include <QThread>
#include <QTimer>

#include <iostream>

// ═══════════════════════════════════════════════════════════════════
//  Construction / Destruction
// ═══════════════════════════════════════════════════════════════════

Worker::Worker(const std::string& videoPath, QObject* parent)
    : QObject(parent)
    , videoPath_(videoPath)
{
}

Worker::~Worker() {
    if (processTimer_) {
        processTimer_->stop();
        delete processTimer_;
    }
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
    const auto& d = detectors_[currentGaugeIdx_];
    calib.initialized = true;
    calib.state = d.state();
    calib.circleStage = d.circle_stage();
    calib.currentGauge = currentGaugeIdx_;
    calib.totalGauges = detectors_.size();
    calib.calibTrackMin = d.calib_track_min();
    calib.calibTrackMax = d.calib_track_max();
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
    cap_.set(cv::CAP_PROP_POS_FRAMES, 0);

    detectedGauges_ = GaugeDetector::FindGauges(firstFrame_, canny_, acc_);
    prevCanny_ = canny_;
    prevAcc_ = acc_;

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
    emit modeChanged(AppMode::kDetection);
}

// ═══════════════════════════════════════════════════════════════════
//  Detection Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::reRunDetection() {
    detectedGauges_ = GaugeDetector::FindGauges(firstFrame_, canny_, acc_);
    prevCanny_ = canny_;
    prevAcc_ = acc_;

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

void Worker::setManualPlacement(bool enabled) {
    manualPlacement_ = enabled;
    if (!enabled && mode_ == AppMode::kDetection)
        reRunDetection();
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
        for (const auto& g : detectedGauges_) {
            cv::circle(disp, g.center, g.radius, cv::Scalar(0, 255, 0), 2);
        }
        if (manualPending_) {
            cv::circle(disp, manualCenter_, 5, cv::Scalar(0, 255, 255), -1);
        }
        emit frameReady(matToQImage(disp));
        emit detectionUpdated(detectedGauges_.size());

    } else if (mode_ == AppMode::kCalibration && !detectors_.empty()) {
        detectors_[currentGaugeIdx_].HandleClick(x, y);

        auto& d = detectors_[currentGaugeIdx_];
        if (d.state() == GaugeState::kCircleManual &&
            d.circle_stage() == 3 && d.circle_radius() > 0 &&
            d.gauge().radius == 0) {
            d.SetCircle(d.circle_center(), d.circle_radius());
            const auto& g = d.gauge();
            std::cout << "  >> Gauge " << currentGaugeIdx_
                      << " at (" << g.center.x << ", " << g.center.y
                      << "), radius=" << g.radius << "\n";
            calibFrame_ = firstFrame_.clone();
            for (const auto& det : detectors_) {
                if (det.gauge().radius > 0) {
                    cv::circle(calibFrame_, det.gauge().center,
                               det.gauge().radius, det.color(), 2);
                }
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
        if (!detectors_.empty()) {
            const auto& d = detectors_[currentGaugeIdx_];
            cv::circle(disp, d.gauge().center, d.gauge().radius, d.color(), 2);
            if (d.state() == GaugeState::kCalibMax) {
                cv::circle(disp, d.pt_min(), 10, cv::Scalar(0, 255, 255), 2);
            } else if (d.state() == GaugeState::kCalibConfirm) {
                cv::circle(disp, d.pt_min(), 10, cv::Scalar(0, 255, 0), 2);
                cv::circle(disp, d.pt_max(), 10, cv::Scalar(0, 0, 255), 2);
            }
        }
    } else {
        disp = firstFrame_.clone();
    }

    if (!detectors_.empty()) {
        const auto& cur = detectors_[currentGaugeIdx_];
        if (cur.state() == GaugeState::kCircleManual) {
            if (cur.gauge().radius > 0) {
                cv::circle(disp, cur.gauge().center,
                           cur.gauge().radius, cur.color(), 2);
            }
            if (cur.circle_stage() == 2) {
                cv::circle(disp, cur.circle_center(), 5,
                           cv::Scalar(0, 255, 0), -1);
                cv::circle(disp, cur.circle_center(), 30,
                           cv::Scalar(0, 255, 0), 1);
            }
        }
    }

    emit frameReady(matToQImage(disp));
}

void Worker::enterCalibration() {
    mode_ = AppMode::kCalibration;
    emit modeChanged(AppMode::kCalibration);
    publishCalibrationDisplay();
    emit calibUIUpdated(computeCalibUI());
}

void Worker::startCalibration() {
    if (mode_ != AppMode::kCalibration || detectors_.empty()) return;
    currentGaugeIdx_ = 0;
    calibFrame_ = firstFrame_.clone();
    detectors_[0].set_state(GaugeState::kCalibMin);
    std::cout << "  >> Starting calibration for "
              << detectors_.size() << " gauge(s)\n";
    publishCalibrationDisplay();
    emit calibUIUpdated(computeCalibUI());
}

void Worker::confirmGauges() {
    if (mode_ != AppMode::kDetection || detectedGauges_.empty())
        return;

    detectors_.clear();
    for (auto& g : detectedGauges_) {
        detectors_.emplace_back(g.center, g.radius, GaugeDetector::NextColor());
        detectors_.back().set_state(GaugeState::kCircleManual);
        detectors_.back().set_circle_stage(3);
        std::cout << "  >> Gauge " << (detectors_.size() - 1)
                  << " at (" << g.center.x << ", " << g.center.y
                  << "), radius=" << g.radius << "\n";
    }
    detectedGauges_.clear();
    calibFrame_ = firstFrame_.clone();
    currentGaugeIdx_ = 0;
    enterCalibration();
}

void Worker::addManual() {
    if (mode_ != AppMode::kDetection) return;

    if (!detectedGauges_.empty()) {
        detectors_.clear();
        for (auto& g : detectedGauges_) {
            detectors_.emplace_back(g.center, g.radius,
                                    GaugeDetector::NextColor());
            detectors_.back().set_state(GaugeState::kCircleManual);
            detectors_.back().set_circle_stage(3);
            std::cout << "  >> Gauge " << (detectors_.size() - 1)
                      << " at (" << g.center.x << ", " << g.center.y
                      << "), radius=" << g.radius << "\n";
        }
        detectedGauges_.clear();
    }

    detectors_.emplace_back();
    detectors_.back().set_color(GaugeDetector::NextColor());
    detectors_.back().set_state(GaugeState::kCircleManual);
    detectors_.back().set_circle_stage(1);
    currentGaugeIdx_ = detectors_.size() - 1;
    calibFrame_ = firstFrame_.clone();
    std::cout << "  >> Add manual gauge " << currentGaugeIdx_ << "\n";
    enterCalibration();
}

void Worker::addAnotherGauge() {
    if (mode_ != AppMode::kCalibration) return;

    detectors_.emplace_back();
    detectors_.back().set_color(GaugeDetector::NextColor());
    detectors_.back().set_state(GaugeState::kCircleManual);
    detectors_.back().set_circle_stage(1);
    currentGaugeIdx_ = detectors_.size() - 1;
    std::cout << "  >> Add manual gauge " << currentGaugeIdx_ << "\n";

    publishCalibrationDisplay();
    emit calibUIUpdated(computeCalibUI());
}

void Worker::confirmCalib() {
    if (mode_ != AppMode::kCalibration || detectors_.empty()) return;

    auto& d = detectors_[currentGaugeIdx_];
    d.CalibrateFromPoints(d.pt_min(), d.pt_max());
    d.SetCalibrationValues(calibMinVal_, calibMaxVal_);
    d.SetCalibrationValid(true);

    const auto& s = d.scale();
    std::cout << "  >> Gauge " << currentGaugeIdx_
              << " scale: " << s.min_value << " at "
              << (s.start_angle * 180.0 / kPi) << " deg, "
              << s.max_value << " at "
              << (s.end_angle * 180.0 / kPi) << " deg\n";

    if (currentGaugeIdx_ + 1 < detectors_.size()) {
        currentGaugeIdx_++;
        calibFrame_ = firstFrame_.clone();
        detectors_[currentGaugeIdx_].set_state(GaugeState::kCalibMin);
        publishCalibrationDisplay();
        emit calibUIUpdated(computeCalibUI());
    } else {
        enterProcessing();
    }
}

void Worker::setCalibMin(int value) {
    calibMinVal_ = value;
}

void Worker::setCalibMax(int value) {
    calibMaxVal_ = value;
}

// ═══════════════════════════════════════════════════════════════════
//  Processing Mode
// ═══════════════════════════════════════════════════════════════════

void Worker::enterProcessing() {
    std::string outputPath =
        videoPath_.substr(0, videoPath_.find_last_of('.')) + "_output.avi";
    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    writer_.open(outputPath, fourcc, fps_, firstFrame_.size());
    if (writer_.isOpened())
        std::cout << "  >> Output: " << outputPath << "\n";

    cap_.set(cv::CAP_PROP_POS_FRAMES, 0);
    for (auto& d : detectors_)
        d.set_state(GaugeState::kProcessing);
    frameCount_ = 0;

    mode_ = AppMode::kProcessing;
    emit modeChanged(AppMode::kProcessing);

    if (!processTimer_)
        processTimer_ = new QTimer(this);
    processTimer_->singleShot(0, this, &Worker::processNextFrame);
}

void Worker::processNextFrame() {
    if (quit_ || mode_ != AppMode::kProcessing) return;

    cv::Mat frame;
    if (!cap_.read(frame)) {
        std::cout << "End of video.\n";
        return;
    }

    int labelY = 60;
    for (auto& d : detectors_) {
        d.DetectNeedle(frame);
        d.DrawOverlay(frame, labelY);
        labelY += 30;
    }

    if (writer_.isOpened()) writer_.write(frame);
    frameCount_++;

    QVector<double> values(detectors_.size());
    for (size_t i = 0; i < detectors_.size(); i++)
        values[i] = detectors_[i].GetSmoothedValue();

    emit frameReady(matToQImage(frame));
    emit gaugeValuesUpdated(values);
    emit frameCountUpdated(frameCount_, totalFrames_);

    QTimer::singleShot(0, this, &Worker::processNextFrame);
}

void Worker::restart() {
    if (mode_ != AppMode::kProcessing) return;
    cap_.set(cv::CAP_PROP_POS_FRAMES, 0);
    for (auto& d : detectors_) d.ResetSmoothing();
    frameCount_ = 0;
    QTimer::singleShot(0, this, &Worker::processNextFrame);
}

// ═══════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════

void Worker::quit() {
    quit_ = true;
    cap_.release();
    if (writer_.isOpened()) writer_.release();
    emit finished();
    QThread::currentThread()->quit();
}
