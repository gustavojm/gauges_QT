#pragma once

#include <QObject>

#include <deque>
#include <opencv2/opencv.hpp>

struct ScaleCalibration {
    double start_angle = 0;
    double end_angle = 0;
    double min_value = 0;
    double max_value = 1000;
    bool valid = false;
};

// ─── Named constants ─────────────────────────────────────────────
inline constexpr double kPi = 3.14159265358979323846;
inline constexpr double kRadiusInset = 0.85;
inline constexpr int kManualCenterRadius = 5;
inline constexpr int kManualGuideRadius = 30;

// Detection: gauge finding
inline constexpr int kMinRadiusDivisor = 15;
inline constexpr int kMaxRadiusDivisor = 2;
inline constexpr int kGaussianBlurKernel = 9;
inline constexpr double kGaussianBlurSigma = 2.0;
inline constexpr int kHoughDpDivisor = 6;
inline constexpr size_t kMaxCirclesToKeep = 5;
inline constexpr double kDuplicateDistFactor = 0.3;

// Detection: colored needle
inline constexpr double kMinNeedleAreaFactor = 0.5;
inline constexpr double kMaxCentroidDistFactor = 0.6;
inline constexpr int kMorphKernelSize = 5;

// Detection: radial needle
inline constexpr int kRadialScanAngles = 360;
inline constexpr double kRadialScanStartFactor = 0.08;
inline constexpr int kAdaptiveThreshBlockSize = 25;
inline constexpr int kAdaptiveThreshC = 8;
inline constexpr double kNeedleDensityWeight = 0.4;
inline constexpr double kNeedleReachWeight = 0.6;

// Motion compensation (optical flow)
inline constexpr int kMaxFeatures = 100;
inline constexpr double kFeatureQuality = 0.01;
inline constexpr int kFeatureBlockSize = 7;
inline constexpr double kMinFeatureDist = 5.0;
inline constexpr size_t kMinPointsForTransform = 6;
inline constexpr double kInlierReprojThresh = 3.0;

// Overlay drawing
inline constexpr int kCircleThickness = 2;
inline constexpr int kNeedleThickness = 3;
inline constexpr double kNeedleLengthFactor = 0.8;
inline constexpr int kCenterDotRadius = 5;
inline constexpr int kCalibPtRadius = 10;
inline constexpr int kCalibPtOutlineRadius = 14;
inline constexpr int kCalibCenterDotRadius = 4;
inline constexpr double kHitTestMinThresh = 12.0;

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

    // ─── Motion Compensation ────────────────────────────────────────
    void InitMotionFeatures(const cv::Mat& frame);
    void UpdateROI(const cv::Mat& frame);

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
    // Result of scanning a single radial line for needle detection.
    struct RadialScanResult {
        double density;
        double reach;
    };

    ROI roi_ = {};
    ROI roi_ref_ = {};              // Original ROI from detection
    ScaleCalibration scale_;

    // Color
    cv::Scalar color_ = {0, 255, 0};

    cv::Point pt_min_, pt_max_;

    cv::Mat CreateMask(const cv::Mat& frame) const;
    double DetectColoredNeedle(const cv::Mat& frame) const;
    double DetectNeedleRadial(const cv::Mat& frame) const;
    RadialScanResult ScanRadialLine(const cv::Mat& binary,
                                    const cv::Mat& mask, double angle) const;

    std::deque<double> value_history_;
    const size_t smooth_window_ = 5;
    double angle_ = -1;
    double value_ = -1;
    double smoothed_value_ = 0;
    int number_ = 0;
    static int next_number_;

    // Motion compensation state
    cv::Mat ref_gray_;                        // Grayscale reference frame
    std::vector<cv::Point2f> ref_features_;   // Feature points on reference frame
    cv::Mat prev_gray_;                       // Previous frame grayscale
    std::vector<cv::Point2f> prev_features_;  // Tracked points from previous frame
};

// ─── Shared Types ─────────────────────────────────────────────────

enum class AppMode { kDetection, kCalibration, kProcessing };

Q_DECLARE_METATYPE(AppMode)
