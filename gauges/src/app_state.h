#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

#include "gauge_detector.h"

enum class AppMode { kDetection, kCalibration, kProcessing };

struct AppState {
    AppMode mode = AppMode::kDetection;

    std::vector<GaugeDetector> detectors;
    std::vector<GaugeROI> detectedGauges;
    size_t currentGaugeIdx = 0;
    cv::Mat calibFrame;

    int detectCanny = 320;
    int detectAcc = 40;
    int prevCanny = -1;
    int prevAcc = -1;

    cv::VideoCapture cap;
    cv::VideoWriter writer;
    double fps = 0;
    int totalFrames = 0;
    int frameCount = 0;
    bool quit = false;
};
