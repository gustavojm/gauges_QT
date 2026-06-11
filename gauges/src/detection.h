#pragma once

#include <opencv2/opencv.hpp>
#include <vector>

struct GaugeROI {
    cv::Point center;
    int radius;
};

struct ScaleCalibration {
    double startAngle;
    double endAngle;
    double minValue;
    double maxValue;
    bool valid;
};

struct TickMark {
    double angle;
    double angularWidth;
    double prominence;
    double distance;
};

extern const double PI;

// ─── Circle Detection ────────────────────────────────────────────
std::vector<GaugeROI> detectGaugeCirclesAuto(const cv::Mat &frame);

// ─── Mask ─────────────────────────────────────────────────────────
cv::Mat createGaugeMask(const cv::Mat &frame, const GaugeROI &gauge);

// ─── Needle Detection ─────────────────────────────────────────────
double detectColoredNeedle(const cv::Mat &frame, const GaugeROI &gauge);
double detectNeedleRadial(const cv::Mat &frame, const GaugeROI &gauge);
double detectNeedleAngle(const cv::Mat &frame, const GaugeROI &gauge);

// ─── Ring Scan ────────────────────────────────────────────────────
std::vector<TickMark> scanRingAtRadius(const cv::Mat &frame, const GaugeROI &gauge,
                                        double scanRadius, int numAngles = 720);
std::vector<TickMark> refineEvenSpacing(const std::vector<TickMark> &marks);

// ─── Angle-to-Value ───────────────────────────────────────────────
double angleToValue(double needleAngle, const ScaleCalibration &scale,
                    const GaugeROI &gauge);

// ─── Overlay Drawing ──────────────────────────────────────────────
void drawOverlay(cv::Mat &frame, const GaugeROI &gauge,
                 double needleAngle, double value,
                 const ScaleCalibration &scale);
