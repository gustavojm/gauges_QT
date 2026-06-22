#pragma once

#include <deque>
#include <memory>
#include <opencv2/opencv.hpp>

// ─── Shared Types ─────────────────────────────────────────────────

enum class AppMode { kDetection, kCalibration, kProcessing };

// ─── Named constants ─────────────────────────────────────────────

inline constexpr double kPi = 3.14159265358979323846;

// Overlay drawing
inline constexpr int kCircleThickness = 2;
inline constexpr int kNeedleThickness = 3;
inline constexpr double kNeedleLengthFactor = 0.8;
inline constexpr int kCenterDotRadius = 5;
inline constexpr int kCalibPtRadius = 10;
inline constexpr int kCalibPtOutlineRadius = 14;
inline constexpr int kCalibCenterDotRadius = 4;
inline constexpr double kHitTestMinThresh = 12.0;

// ─── Gauge Base Class ────────────────────────────────────────────

class Gauge {

public:
    // Simple region of interest — center + bounding radius.
    // Subclasses may extend with shape-specific geometry.
    struct ROI {
        cv::Point center;
        int radius = 0;
    };

    // Calibration marker constants
    static constexpr int kMarkerNone = -1;
    static constexpr int kMarkerMin  = 0;
    static constexpr int kMarkerMax  = 1;

    Gauge() noexcept = default;
    Gauge(const cv::Point& center, int radius, const cv::Scalar& color);
    virtual ~Gauge() = default;

    // ─── Static: Color palette ────────────────────────────────────
    static cv::Scalar NextColor();

    // ─── Calibration (generic) ────────────────────────────────────
    virtual void SetCalibrationValues(double minVal, double maxVal);
    double minValue() const { return min_value_; }
    double maxValue() const { return max_value_; }

    // ─── Smoothing ────────────────────────────────────────────────
    void AddReading(double value);
    double smoothedValue() const { return smoothed_value_; }
    void ResetSmoothing();

    // ─── State Access ─────────────────────────────────────────────
    int number() const { return number_; }
    const cv::Scalar& color() const { return color_; }
    const ROI& roi() const { return roi_; }

    // ─── Drawing (generic) ────────────────────────────────────────
    void DrawGaugeNumber(cv::Mat& img) const;

    // ─── Pure virtual interface (shape-specific) ──────────────────
    virtual void InitMotionFeatures(const cv::Mat& frame) = 0;
    virtual void UpdateROI(const cv::Mat& frame) = 0;
    virtual double DetectNeedle(const cv::Mat& frame) = 0;
    virtual void FinalizeCalibration() = 0;
    virtual void DrawOverlay(cv::Mat& frame, int labelY = 60) = 0;
    virtual void DrawCalibrationOverlay(cv::Mat& frame) = 0;
    virtual void DrawOutline(cv::Mat& img) const = 0;
    virtual int  HandleClick(int clickX, int clickY) = 0;
    virtual void MoveMarker(int which, cv::Point click) = 0;

protected:
    // ROI (center + bounding radius)
    ROI roi_ = {};
    ROI roi_ref_ = {};

    // Color
    cv::Scalar color_ = {0, 255, 0};

    // Calibration markers
    cv::Point pt_min_, pt_max_;
    double min_value_ = 0;
    double max_value_ = 1000;
    bool calib_values_set_ = false;

    // Smoothing
    std::deque<double> value_history_;
    size_t smooth_window_ = 5;
    double value_ = -1;
    double smoothed_value_ = 0;

    // Gauge identification
    int number_ = 0;
    static int next_number_;

    // Motion compensation state
    cv::Mat ref_gray_;
    std::vector<cv::Point2f> ref_features_;
    cv::Mat prev_gray_;
    std::vector<cv::Point2f> prev_features_;
};

// ─── Motion compensation constants (used by all gauge types) ─────

inline constexpr int kMaxFeatures = 100;
inline constexpr double kFeatureQuality = 0.01;
inline constexpr int kFeatureBlockSize = 7;
inline constexpr double kMinFeatureDist = 5.0;
inline constexpr size_t kMinPointsForTransform = 6;
inline constexpr double kInlierReprojThresh = 3.0;

// ─── Qt metatype ─────────────────────────────────────────────────
// Note: Q_DECLARE_METATYPE(AppMode) is in circular_gauge.h where QObject is included.
