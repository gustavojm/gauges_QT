#pragma once

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

enum class GaugeState {
    kInit,
    kCircleManual,
    kCalibMin,
    kCalibMax,
    kCalibConfirm,
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
    void set_state(GaugeState s) { state_ = s; }

    int circle_stage() const { return circle_stage_; }
    void set_circle_stage(int stage) { circle_stage_ = stage; }
    const cv::Point& circle_center() const { return circle_center_; }
    void set_circle_center(const cv::Point& pt) { circle_center_ = pt; }
    int circle_radius() const { return circle_radius_; }
    void set_circle_radius(int r) { circle_radius_ = r; }

    const cv::Point& pt_min() const { return pt_min_; }
    void set_pt_min(const cv::Point& pt) { pt_min_ = pt; }
    const cv::Point& pt_max() const { return pt_max_; }
    void set_pt_max(const cv::Point& pt) { pt_max_ = pt; }

    int calib_track_min() const { return calib_track_min_; }
    void set_calib_track_min(int v) { calib_track_min_ = v; }
    int calib_track_max() const { return calib_track_max_; }
    void set_calib_track_max(int v) { calib_track_max_ = v; }

    // ─── Circle ─────────────────────────────────────────────────────
    void SetCircle(const cv::Point& center, int radius);

    // ─── Needle Detection ───────────────────────────────────────────
    double DetectNeedle(const cv::Mat& frame);

    // ─── Calibration ────────────────────────────────────────────────
    void CalibrateFromPoints(const cv::Point& pt_min, const cv::Point& pt_max);
    void SetCalibrationValues(double minVal, double maxVal);
    void SetCalibrationValid(bool valid);

    // ─── Value Mapping ──────────────────────────────────────────────
    double AngleToValue(double needleAngle) const;

    // ─── Overlay Drawing ────────────────────────────────────────────
    void DrawOverlay(cv::Mat& frame, int labelY = 60) const;

    // ─── State Access ───────────────────────────────────────────────
    const GaugeROI& gauge() const { return gauge_; }
    const ScaleCalibration& scale() const { return scale_; }

    // ─── Color ───────────────────────────────────────────────────────
    const cv::Scalar& color() const { return color_; }
    void set_color(const cv::Scalar& c) { color_ = c; }
    static cv::Scalar NextColor();

    // ─── Smoothing ──────────────────────────────────────────────────
    double SmoothReadings(double value);
    double GetSmoothedValue() const;
    void ResetSmoothing() {
        value_history_.clear();
        smoothed_value_ = 0;
    }

    // Handle a click coming from the UI while in manual / calibration modes.
    // Behavior matches previous inline code in gauge_reader.cpp:
    //  - CIRCLE_MANUAL stage 1 -> set center, advance to stage 2
    //  - CIRCLE_MANUAL stage 2 -> set radius, advance to stage 3
    //  - CALIB_MIN -> set pt_min, advance to CALIB_MAX
    //  - CALIB_MAX -> set pt_max, advance to CALIB_CONFIRM
    void HandleClick(int clickX, int clickY);

private:
    GaugeROI gauge_{};
    ScaleCalibration scale_{0, 0, 0, 1000, false};

    // Color
    cv::Scalar color_{0, 255, 0};

    // State machine
    GaugeState state_ = GaugeState::kInit;
    int circle_stage_ = 0;
    cv::Point circle_center_;
    int circle_radius_ = 0;
    cv::Point pt_min_, pt_max_;
    int calib_track_min_ = 0;
    int calib_track_max_ = 1000;

    cv::Mat CreateMask(const cv::Mat& frame) const;
    double DetectColoredNeedle(const cv::Mat& frame) const;
    double DetectNeedleRadial(const cv::Mat& frame) const;

    std::deque<double> value_history_;
    const int smooth_window_ = 5;
    double angle_ = -1;
    double value_ = -1;
    double smoothed_value_ = 0;
};
