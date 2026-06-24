#pragma once

#include "gauge.h"

#include <QObject>
#include <optional>

Q_DECLARE_METATYPE(AppMode)

// ─── CircularGauge-specific constants ────────────────────────────

inline constexpr double kRadiusInset = 0.85;
inline constexpr int kManualCenterRadius = 5;
inline constexpr int kManualGuideRadius = 30;

// Detection: gauge finding
inline constexpr int kMinRadiusDivisor = 15;
inline constexpr int kMaxRadiusDivisor = 2;
inline constexpr int kGaussianBlurKernel = 9;
inline constexpr double kGaussianBlurSigma = 2.0;
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

// ─── CircularGauge ───────────────────────────────────────────────

class CircularGauge : public Gauge {

public:
    // Extended ROI with ellipse + homography (circular-specific)
    struct ROI {
        cv::Point center;
        int radius = 0;
        cv::RotatedRect ellipse;
        bool hasEllipse = false;
        cv::Mat H;
        cv::Size outSize;
    };

    // Angle-based scale calibration (circular-specific)
    struct ScaleCalibration {
        double start_angle = 0;
        double end_angle = 0;
        double min_value = 0;
        double max_value = 1000;
        bool valid = false;
    };

    CircularGauge() noexcept = default;
    CircularGauge(const cv::Point& center, int radius, const cv::Scalar& color);

    // ─── Static: Find all gauges in frame ─────────────────────────
    static std::vector<ROI> FindGauges(const cv::Mat& frame,
                                       int cannyThreshold,
                                       int accumulatorThreshold);

    // ─── Ellipse-to-Circle Homography ─────────────────────────────
    static bool ComputeHomography(const std::vector<cv::Point>& pts,
                                  cv::Mat& H, cv::Size& outSize,
                                  cv::RotatedRect& ellipseRect,
                                  cv::Point& inferredCenter);
    static bool HomographyFromEllipse(const cv::RotatedRect& rr,
                                      cv::Mat& H, cv::Size& outSize,
                                      cv::Point& inferredCenter);
    void SetHomography(const cv::Mat& H, const cv::Size& outSize,
                       cv::Point center, cv::RotatedRect ellipseRect = {});
    cv::Mat WarpFrame(const cv::Mat& frame) const;
    bool has_homography() const { return hasHomography_; }
    const cv::Mat& homography() const { return homography_; }
    const cv::Size& warp_size() const { return warpSize_; }

    // ─── Gauge interface overrides ────────────────────────────────
    void InitMotionFeatures(const cv::Mat& frame) override;
    void UpdateROI(const cv::Mat& frame) override;
    std::optional<double> DetectNeedle(const cv::Mat& frame) override;
    void FinalizeCalibration() override;
    void SetCalibrationValues(double minVal, double maxVal) override;
    void DrawOverlay(cv::Mat& frame, int labelY = 60) const override;
    void DrawCalibrationOverlay(cv::Mat& frame) override;
    void DrawOutline(cv::Mat& img) const override;
    int  HandleClick(int clickX, int clickY) override;
    void MoveMarker(int which, cv::Point click) override;
    void ResetMotionState() override;

    // ─── Circular-specific: angle-to-value ────────────────────────
    std::optional<double> AngleToValue(double needleAngle) const;

    // ─── Circular-specific: manual placement ──────────────────────
    static constexpr int kManualClicks = 5;
    static const char* manualInstruction(int stage);
    static std::optional<ROI> FitFromManualEdges(
        const std::vector<cv::Point>& edges);

    // ─── Circular-specific: scale access ──────────────────────────
    const ScaleCalibration& scale() const { return scale_; }

private:
    // Result of scanning a single radial line for needle detection.
    struct RadialScanResult {
        double density;
        double reach;
    };

    // Shared eigendecomposition + homography construction for ComputeHomography
    // and HomographyFromEllipse. Only R differs between the two callers.
    static bool buildHomographyFromEllipse(float cx, float cy,
                                           float a, float b,
                                           double theta, double R,
                                           cv::Mat& H, cv::Size& outSize);

    ScaleCalibration scale_;

    cv::Mat CreateMask(const cv::Mat& frame) const;
    cv::Point detectionCenter() const;
    double DetectColoredNeedle(const cv::Mat& frame) const;
    double DetectNeedleRadial(const cv::Mat& frame) const;
    RadialScanResult ScanRadialLine(const cv::Mat& binary,
                                    const cv::Mat& mask, double angle) const;

    double angle_ = -1;

    // Ellipse-to-circle homography state
    cv::Mat homography_;
    cv::Mat homographyBase_;
    cv::Size warpSize_;
    cv::Point2f rectCenter_;
    cv::RotatedRect ellipseRect_;
    bool hasHomography_ = false;
};
