#include "worker.h"
#include "shared_state.h"
#include "gauge_detector.h"

#include <chrono>
#include <iostream>
#include <thread>

// ── Helpers ──────────────────────────────────────────────────────

static cv::Mat BuildDetectionDisplay(const cv::Mat& frame,
                                     const std::vector<GaugeROI>& gauges) {
    cv::Mat disp = frame.clone();
    static const std::vector<cv::Scalar> palette = {
        {0, 0, 255},   {255, 0, 0},   {0, 255, 255},
        {255, 0, 255}, {255, 255, 0}, {0, 165, 255},
    };
    for (size_t i = 0; i < gauges.size(); i++) {
        const auto& col = palette[i % palette.size()];
        cv::circle(disp, gauges[i].center, gauges[i].radius, col, 2);
        cv::putText(disp, std::to_string(gauges[i].radius),
                    gauges[i].center + cv::Point(-20, 5),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, col, 1);
    }
    return disp;
}

static cv::Mat BuildCalibrationDisplay(const cv::Mat& firstFrame,
                                       const cv::Mat& calibFrame,
                                       const std::vector<GaugeDetector>& detectors,
                                       size_t currentGaugeIdx) {
    cv::Mat disp;
    if (!calibFrame.empty()) {
        disp = calibFrame.clone();
        if (!detectors.empty()) {
            const auto& d = detectors[currentGaugeIdx];
            cv::circle(disp, d.gauge().center, d.gauge().radius, d.color(), 2);
            if (d.state() == GaugeState::kCalibMax) {
                cv::circle(disp, d.pt_min(), 10, cv::Scalar(0, 255, 255), 2);
            } else if (d.state() == GaugeState::kCalibConfirm) {
                cv::circle(disp, d.pt_min(), 10, cv::Scalar(0, 255, 0), 2);
                cv::circle(disp, d.pt_max(), 10, cv::Scalar(0, 0, 255), 2);
            }
        }
    } else {
        disp = firstFrame.clone();
        if (!detectors.empty()) {
            const auto& d = detectors[0];
            if (d.state() == GaugeState::kCircleManual &&
                d.circle_stage() == 2) {
                cv::circle(disp, d.circle_center(), 5, cv::Scalar(0, 255, 0), -1);
                cv::circle(disp, d.circle_center(), 30, cv::Scalar(0, 255, 0), 1);
            }
        }
    }
    return disp;
}

// ── Worker Main ──────────────────────────────────────────────────

void WorkerMain(const std::string& videoPath, SharedState& shared) {
    cv::VideoCapture cap;
    cap.open(videoPath);
    if (!cap.isOpened()) {
        std::cerr << "Worker: Could not open video\n";
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.quit = true;
        return;
    }

    int totalFrames =
        static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    double fps = cap.get(cv::CAP_PROP_FPS);

    cv::Mat firstFrame;
    if (!cap.read(firstFrame)) {
        std::cerr << "Worker: Could not read first frame\n";
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.quit = true;
        return;
    }
    cap.set(cv::CAP_PROP_POS_FRAMES, 0);

    cv::Mat frame = firstFrame.clone();
    cv::Mat calibFrame;
    std::vector<GaugeDetector> detectors;
    std::vector<GaugeROI> detectedGauges;
    size_t currentGaugeIdx = 0;
    int prevCanny = -1, prevAcc = -1;
    int frameCount = 0;
    cv::VideoWriter writer;

    auto publishCalibUI = [&](std::lock_guard<std::mutex>&) {
        if (detectors.empty()) {
            shared.calibUI.initialized = false;
            return;
        }
        size_t idx = detectedGauges.empty() ? 0 : currentGaugeIdx;
        const auto& d = detectors[idx];
        shared.calibUI.initialized = true;
        shared.calibUI.state = d.state();
        shared.calibUI.circleStage = d.circle_stage();
        shared.calibUI.currentGauge = currentGaugeIdx;
        shared.calibUI.totalGauges = detectors.size();
        shared.calibUI.calibTrackMin = d.calib_track_min();
        shared.calibUI.calibTrackMax = d.calib_track_max();
    };

    while (true) {
        // ── Read commands from main thread ─────────────────────
        AppMode mode;
        WorkerCommand cmd;
        int canny, acc;
        bool doDetection;
        bool hasClick = false;
        int clickX = 0, clickY = 0;
        int calMin = 0, calMax = 1000;
        {
            std::lock_guard<std::mutex> lk(shared.mtx);
            if (shared.quit) break;
            mode = shared.mode;
            cmd = shared.command;
            shared.command = WorkerCommand::kNone;
            canny = shared.detectCanny;
            acc = shared.detectAcc;
            doDetection = shared.runDetection;
            shared.runDetection = false;
            if (shared.hasClick) {
                hasClick = true;
                clickX = shared.clickX;
                clickY = shared.clickY;
                shared.hasClick = false;
            }
            calMin = shared.calibMinVal;
            calMax = shared.calibMaxVal;
        }

        // ── Process by mode ────────────────────────────────────
        cv::Mat disp;

        switch (mode) {
            // ═══════ Detection ══════════════════════════════════
            case AppMode::kDetection: {
                if (doDetection || canny != prevCanny ||
                    acc != prevAcc) {
                    detectedGauges = GaugeDetector::FindGauges(
                        firstFrame, canny, acc);
                    prevCanny = canny;
                    prevAcc = acc;
                }
                disp = BuildDetectionDisplay(firstFrame, detectedGauges);

                std::lock_guard<std::mutex> lk(shared.mtx);
                disp.copyTo(shared.displayFrame);
                shared.detectedGauges = detectedGauges;
                shared.frameReady = true;
                break;
            }

            // ═══════ Calibration ═══════════════════════════════
            case AppMode::kCalibration: {
                if (cmd == WorkerCommand::kConfirmGauges) {
                    detectors.clear();
                    for (auto& g : detectedGauges) {
                        detectors.emplace_back(
                            g.center, g.radius,
                            GaugeDetector::NextColor());
                        std::cout << "  >> Gauge " << (detectors.size() - 1)
                                  << " at (" << g.center.x << ", " << g.center.y
                                  << "), radius=" << g.radius << "\n";
                    }
                    calibFrame = firstFrame.clone();
                    for (auto& d : detectors) {
                        cv::circle(calibFrame, d.gauge().center,
                                   d.gauge().radius, d.color(), 2);
                    }
                    currentGaugeIdx = 0;
                } else if (cmd == WorkerCommand::kStartManual) {
                    detectors.clear();
                    detectors.emplace_back();
                    detectors[0].set_color(GaugeDetector::NextColor());
                    detectors[0].set_state(GaugeState::kCircleManual);
                    detectors[0].set_circle_stage(1);
                    std::cout << "  >> Manual circle placement.\n";
                    std::cout << "  >> Click center, then edge.\n";
                }

                // Handle click
                if (hasClick && !detectors.empty()) {
                    size_t idx = detectedGauges.empty() ? 0 : currentGaugeIdx;
                    detectors[idx].HandleClick(clickX, clickY);
                }

                // Auto-transition: manual circle stage 3 → CALIB_MIN
                if (detectedGauges.empty() && !detectors.empty()) {
                    auto& d = detectors[0];
                    if (d.state() == GaugeState::kCircleManual &&
                        d.circle_stage() == 3 && d.circle_radius() > 0) {
                        d.SetCircle(d.circle_center(), d.circle_radius());
                        const auto& g = d.gauge();
                        std::cout << "  >> Gauge at (" << g.center.x << ", "
                                  << g.center.y << "), radius=" << g.radius
                                  << "\n";
                        calibFrame = firstFrame.clone();
                        cv::circle(calibFrame, g.center, g.radius, d.color(),
                                   2);
                        d.set_state(GaugeState::kCalibMin);
                    }
                }

                // Handle calibration confirmation command
                if (cmd == WorkerCommand::kConfirmCalib && !detectors.empty()) {
                    size_t idx =
                        detectedGauges.empty() ? 0 : currentGaugeIdx;
                    auto& d = detectors[idx];
                    d.CalibrateFromPoints(d.pt_min(), d.pt_max());
                    d.SetCalibrationValues(calMin, calMax);
                    d.SetCalibrationValid(true);

                    const auto& s = d.scale();
                    std::cout << "  >> Gauge " << idx
                              << " scale: " << s.min_value << " at "
                              << (s.start_angle * 180.0 / kPi) << " deg, "
                              << s.max_value << " at "
                              << (s.end_angle * 180.0 / kPi) << " deg\n";

                    if (currentGaugeIdx + 1 < detectors.size()) {
                        currentGaugeIdx++;
                        detectors[currentGaugeIdx].set_state(
                            GaugeState::kCalibMin);
                    } else {
                        std::string outputPath =
                            videoPath.substr(0,
                                             videoPath.find_last_of('.')) +
                            "_output.avi";
                        int fourcc =
                            cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
                        writer.open(outputPath, fourcc, fps,
                                    firstFrame.size());
                        if (writer.isOpened())
                            std::cout << "  >> Output: " << outputPath
                                      << "\n";
                        cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                        for (auto& det : detectors)
                            det.set_state(GaugeState::kProcessing);
                        frameCount = 0;

                        std::lock_guard<std::mutex> lk(shared.mtx);
                        shared.mode = AppMode::kProcessing;
                    }
                }

                // Check if all detectors are in processing state
                // (happens automatically after ConfirmCalib + mode change)
                bool allProcessing = true;
                for (auto& d : detectors)
                    if (d.state() != GaugeState::kProcessing) {
                        allProcessing = false;
                        break;
                    }
                if (allProcessing && !detectors.empty()) {
                    std::lock_guard<std::mutex> lk(shared.mtx);
                    shared.mode = AppMode::kProcessing;
                }

                disp = BuildCalibrationDisplay(firstFrame, calibFrame,
                                               detectors, currentGaugeIdx);

                {
                    std::lock_guard<std::mutex> lk(shared.mtx);
                    disp.copyTo(shared.displayFrame);
                    shared.frameReady = true;
                    publishCalibUI(lk);
                }
                break;
            }

            // ═══════ Processing ════════════════════════════════
            case AppMode::kProcessing: {
                if (cmd == WorkerCommand::kRestart) {
                    cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                    for (auto& d : detectors) d.ResetSmoothing();
                    frameCount = 0;
                    if (!cap.read(frame)) {
                        std::lock_guard<std::mutex> lk(shared.mtx);
                        shared.quit = true;
                        break;
                    }
                } else {
                    if (!cap.read(frame)) {
                        std::cout << "End of video.\n";
                        std::lock_guard<std::mutex> lk(shared.mtx);
                        shared.quit = true;
                        break;
                    }
                }

                int labelY = 60;
                for (auto& d : detectors) {
                    d.DetectNeedle(frame);
                    d.DrawOverlay(frame, labelY);
                    labelY += 30;
                }

                if (writer.isOpened()) writer.write(frame);
                frameCount++;
                disp = frame.clone();

                {
                    std::lock_guard<std::mutex> lk(shared.mtx);
                    disp.copyTo(shared.displayFrame);
                    shared.gaugeValues.resize(detectors.size());
                    for (size_t i = 0; i < detectors.size(); i++)
                        shared.gaugeValues[i] =
                            detectors[i].GetSmoothedValue();
                    shared.frameCount = frameCount;
                    shared.totalFrames = totalFrames;
                    shared.frameReady = true;
                }
                break;
            }
        }

        // Small sleep to prevent busy-wait burn when paused
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    cap.release();
    if (writer.isOpened()) writer.release();
    std::cout << "Worker thread finished.\n";
}
