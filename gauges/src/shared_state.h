#pragma once

#include <mutex>
#include <opencv2/opencv.hpp>
#include <vector>

#include "app_state.h"
#include "gauge_detector.h"

enum class WorkerCommand {
    kNone,
    kConfirmGauges,
    kStartManual,
    kConfirmCalib,
    kCancel,
    kRestart,
};

struct CalibUIState {
    GaugeState state = GaugeState::kInit;
    int circleStage = 0;
    size_t currentGauge = 0;
    size_t totalGauges = 0;
    bool initialized = false;
    int calibTrackMin = 0;
    int calibTrackMax = 1000;
};

struct SharedState {
    std::mutex mtx;

    // ── Worker → Main ──────────────────────────────────────────
    cv::Mat displayFrame;
    std::vector<double> gaugeValues;
    std::vector<GaugeROI> detectedGauges;
    int frameCount = 0;
    int totalFrames = 0;
    bool frameReady = false;

    CalibUIState calibUI;

    // ── Main → Worker ──────────────────────────────────────────
    bool quit = false;
    AppMode mode = AppMode::kDetection;
    int detectCanny = 320;
    int detectAcc = 40;
    bool runDetection = true;

    WorkerCommand command = WorkerCommand::kNone;

    bool hasClick = false;
    int clickX = 0;
    int clickY = 0;

    int calibMinVal = 0;
    int calibMaxVal = 1000;
};
