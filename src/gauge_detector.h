#pragma once

#include <QObject>

#include <deque>
#include <opencv2/opencv.hpp>

struct GaugeROI {
    cv::Point center;
    int radius;
};

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

enum class GaugeState {
    kInit,
    kCalibrating,
    kProcessing
};

class GaugeDetector {
public:
    GaugeDetector() = default;
    GaugeDetector(const cv::Point& center, int radius, const cv::Scalar& color);

    // ─── Static: Find all gauges in a frame ─────────────────────────
    static std::vector<GaugeROI> FindGauges(const cv::Mat& frame,
                                            int cannyThreshold,
                                            int accumulatorThreshold);

    // ─── State Machine ──────────────────────────────────────────────
    GaugeState state() const { return state_; }    

    // ─── Circle ─────────────────────────────────────────────────────
    void StartProcessing();

    // ─── Needle Detection ───────────────────────────────────────────
    double DetectNeedle(const cv::Mat& frame);

    // ─── Calibration ────────────────────────────────────────────────
    void FinalizeCalibration();
    void SetCalibrationValues(double minVal, double maxVal);

    // ─── Value Mapping ──────────────────────────────────────────────
    double AngleToValue(double needleAngle) const;

    // ─── Overlay Drawing ────────────────────────────────────────────
    void DrawOverlay(cv::Mat& frame, int labelY = 60) const;
    void DrawCalibrationOverlay(cv::Mat& frame, bool highlight = false) const;

    // ─── State Access ───────────────────────────────────────────────
    const ScaleCalibration& scale() const { return scale_; }

    // ─── Color ───────────────────────────────────────────────────────
    const cv::Scalar& color() const { return color_; }
    static cv::Scalar NextColor();
    void DrawGaugeNumber(cv::Mat& img) const;
    void DrawOutline(cv::Mat& img, bool highlight = false) const;

    // ─── Smoothing ──────────────────────────────────────────────────
    double SmoothReadings(double value);
    double GetSmoothedValue() const;
    void ResetSmoothing() {
        value_history_.clear();
        smoothed_value_ = 0;
    }

    // ─── Drag / Hit-test for marker calibration ───────────────────────
    static constexpr int kMarkerNone = -1;
    static constexpr int kMarkerMin  = 0;
    static constexpr int kMarkerMax  = 1;

    // Returns kMarkerMin if click is near pt_min_, kMarkerMax if near pt_max_,
    // or kMarkerNone otherwise.  (threshold = radius/6)
    int HitTestMarker(cv::Point click, int radius) const;

    // Project click onto the circle perimeter and store in pt_min_ / pt_max_.
    void MoveMarkerToPerimeter(int which, cv::Point click);

    // Handle a click coming from the UI while in manual / calibration modes.
    int HandleClick(int clickX, int clickY);

private:
    GaugeROI gauge_ = {};
    ScaleCalibration scale_ = {0, 0, 0, 1000, false};

    // Color
    cv::Scalar color_ = {0, 255, 0};

    // State machine
    GaugeState state_ = GaugeState::kInit;
    cv::Point pt_min_, pt_max_;

    cv::Mat CreateMask(const cv::Mat& frame) const;
    double DetectColoredNeedle(const cv::Mat& frame) const;
    double DetectNeedleRadial(const cv::Mat& frame) const;

    std::deque<double> value_history_;
    const int smooth_window_ = 5;
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
    GaugeState state = GaugeState::kInit;
    size_t currentGauge = 0;
    size_t totalGauges = 0;
    bool initialized = false;
};

Q_DECLARE_METATYPE(CalibUIState)
