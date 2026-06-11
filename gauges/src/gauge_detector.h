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

inline constexpr double PI = 3.14159265358979323846;

class GaugeDetector {
public:
    // ─── Circle Detection ────────────────────────────────────────────
    // Auto-detect gauge face in frame. Returns true if found.
    bool detectCircle(const cv::Mat &frame);

    // Manually set gauge circle (for fallback when auto fails)
    void setCircle(const cv::Point &center, int radius);

    // ─── Needle Detection ─────────────────────────────────────────────
    // Detect needle angle in current frame. Returns angle in radians or -1.
    double detectNeedle(const cv::Mat &frame) const;

    // ─── Calibration ──────────────────────────────────────────────────
    // Compute start/end angles from user-clicked min/max points
    void calibrateFromPoints(const cv::Point &ptMin, const cv::Point &ptMax);

    // Set calibration min/max values
    void setCalibrationValues(double minVal, double maxVal);

    // Mark calibration as valid
    void setCalibrationValid(bool valid);

    // Debug angle tweaking (for ImGui sliders)
    void setStartAngle(double a);
    void setEndAngle(double a);

    // ─── Value Mapping ────────────────────────────────────────────────
    // Convert needle angle to calibrated value
    double angleToValue(double needleAngle) const;

    // ─── Overlay Drawing ──────────────────────────────────────────────
    // Draw gauge circle, scale lines, needle, and value text on frame
    void drawOverlay(cv::Mat &frame, double needleAngle, double value) const;

    // ─── State Access ─────────────────────────────────────────────────
    const GaugeROI &gauge() const { return m_gauge; }
    const ScaleCalibration &scale() const { return m_scale; }
    bool hasGauge() const { return m_gauge.radius > 0; }
    bool hasScale() const { return m_scale.valid; }

    // ─── Ring Scan (static utilities, not used in main flow) ──────────
    static std::vector<TickMark> scanRingAtRadius(const cv::Mat &frame,
                                                   const GaugeROI &gauge,
                                                   double scanRadius,
                                                   int numAngles = 720);
    static std::vector<TickMark> refineEvenSpacing(const std::vector<TickMark> &marks);

private:
    GaugeROI m_gauge{};
    ScaleCalibration m_scale{0, 0, 0, 1000, false};

    cv::Mat createMask(const cv::Mat &frame) const;
    double detectColoredNeedle(const cv::Mat &frame) const;
    double detectNeedleRadial(const cv::Mat &frame) const;
};
