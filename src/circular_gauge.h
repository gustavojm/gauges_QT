#pragma once

#include <QObject>

#include <deque>
#include <opencv2/opencv.hpp>

struct ScaleCalibration {
    double start_angle;
    double end_angle;
    double min_value;
    double max_value;
    bool valid;
};

inline constexpr double kPi = 3.14159265358979323846;
inline constexpr int kManualCenterRadius = 5;
inline constexpr int kManualGuideRadius = 30;
inline constexpr double kRadiusInset = 0.85;

class CircularGauge {

public:
    /* Region of Interest (the sub-area of an image that an 
    algorithm should focus on, as opposed to processing 
    the entire frame). */
    struct ROI {
        cv::Point center;
        int radius;
    };

    CircularGauge() noexcept = default;
    CircularGauge(const cv::Point& center, int radius, const cv::Scalar& color);

    // ─── Static: Find all gauges in a frame ─────────────────────────
    static std::vector<ROI> FindGauges(const cv::Mat& frame,
                                            int cannyThreshold,
                                            int accumulatorThreshold);

    // ─── Needle Detection ───────────────────────────────────────────
    double DetectNeedle(const cv::Mat& frame);

    // ─── Calibration ────────────────────────────────────────────────
    void FinalizeCalibration();
    void SetCalibrationValues(double minVal, double maxVal);

    // ─── Value Mapping ──────────────────────────────────────────────
    double AngleToValue(double needleAngle) const;

    // ─── Overlay Drawing ────────────────────────────────────────────
    void DrawOverlay(cv::Mat& frame, int labelY = 60) const;
    void DrawCalibrationOverlay(cv::Mat& frame) const;

    // ─── State Access ───────────────────────────────────────────────
    const ScaleCalibration& scale() const { return scale_; }

    // ─── Color ───────────────────────────────────────────────────────
    const cv::Scalar& color() const { return color_; }
    static cv::Scalar NextColor();
    void DrawGaugeNumber(cv::Mat& img) const;
    void DrawOutline(cv::Mat& img) const;

    // ─── Smoothing ──────────────────────────────────────────────────
    void AddReading(double value);
    double smoothedValue() const;
    void ResetSmoothing() {
        value_history_.clear();
        smoothed_value_ = 0;
    }

    // ─── Drag / Hit-test for marker calibration ───────────────────────
    static constexpr int kMarkerNone = -1;
    static constexpr int kMarkerMin  = 0;
    static constexpr int kMarkerMax  = 1;

    // Project click onto the circle perimeter and store in pt_min_ / pt_max_.
    void MoveMarker(int which, cv::Point click);

    // Handle a click coming from the UI while in manual / calibration modes.
    int HandleClick(int clickX, int clickY);

private:
    ROI roi_ = {};
    ScaleCalibration scale_ = {0, 0, 0, 1000, false};

    // Color
    cv::Scalar color_ = {0, 255, 0};

    cv::Point pt_min_, pt_max_;

    cv::Mat CreateMask(const cv::Mat& frame) const;
    double DetectColoredNeedle(const cv::Mat& frame) const;
    double DetectNeedleRadial(const cv::Mat& frame) const;

    std::deque<double> value_history_;
    const size_t smooth_window_ = 5;
    double angle_ = -1;
    double value_ = -1;
    double smoothed_value_ = 0;
    int number_ = 0;
    static int next_number_;
};

// ─── Shared Types ─────────────────────────────────────────────────

enum class AppMode { kDetection, kCalibration, kProcessing };

Q_DECLARE_METATYPE(AppMode)

struct CalibUIState {
    size_t totalGauges = 0;
    bool initialized = false;
};

Q_DECLARE_METATYPE(CalibUIState)
