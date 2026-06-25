#pragma once

#include "gauge.h"

#include <QObject>
#include <optional>

Q_DECLARE_METATYPE(AppMode)

// ─── CircularGauge-specific constants ────────────────────────────

/// Inset factor for calibration markers along the gauge radius (0.85 = 85% of radius).
inline constexpr double kRadiusInset = 0.85;
/// Radius of the manual-placement center dot.
inline constexpr int kManualCenterRadius = 5;
/// Radius of the manual-placement guide circle.
inline constexpr int kManualGuideRadius = 30;

// Detection: gauge finding
inline constexpr int kMinRadiusDivisor = 15;           ///< Divisor for minimum detectable radius.
inline constexpr int kMaxRadiusDivisor = 2;            ///< Divisor for maximum detectable radius.
inline constexpr int kGaussianBlurKernel = 9;          ///< Gaussian blur kernel size for edge detection.
inline constexpr double kGaussianBlurSigma = 2.0;      ///< Gaussian blur sigma.
inline constexpr size_t kMaxCirclesToKeep = 5;         ///< Maximum number of circular gauges to keep.
inline constexpr double kDuplicateDistFactor = 0.3;    ///< Fraction of radius used for deduplication distance.

// Detection: colored needle
inline constexpr double kMinNeedleAreaFactor = 0.5;    ///< Minimum needle contour area as fraction of radius.
inline constexpr double kMaxCentroidDistFactor = 0.6;  ///< Maximum centroid distance from center as fraction of radius.
inline constexpr int kMorphKernelSize = 5;             ///< Morphological operation kernel size.

// Detection: radial needle
inline constexpr int kRadialScanAngles = 360;                ///< Number of angles to scan in radial needle detection.
inline constexpr double kRadialScanStartFactor = 0.08;      ///< Start scanning at 8% of radius from center.
inline constexpr int kAdaptiveThreshBlockSize = 25;          ///< Block size for adaptive thresholding.
inline constexpr int kAdaptiveThreshC = 8;                   ///< Constant subtracted from adaptive threshold.
inline constexpr double kNeedleDensityWeight = 0.4;          ///< Weight of pixel density in radial needle score.
inline constexpr double kNeedleReachWeight = 0.6;            ///< Weight of run-length reach in radial needle score.

// ─── CircularGauge ───────────────────────────────────────────────

/**
 * @class CircularGauge
 * @brief Gauge subclass for round dial instruments with a radial needle.
 *
 * Detects circular gauges via ellipse fitting, applies an ellipse-to-circle
 * homography for rectification, and locates the needle using either colour
 * segmentation (red needle) or radial scan-line analysis (dark needle).
 *
 * @see Gauge
 * @see EdgewiseGauge
 */
class CircularGauge : public Gauge {

public:
    /**
     * @struct ROI
     * @brief Extended region of interest with ellipse geometry and homography.
     *
     * Inherits the concept of Gauge::ROI but adds ellipse-specific data
     * needed for the perspective rectification pipeline.
     */
    struct ROI {
        cv::Point center;           ///< Center of the gauge face.
        int radius = 0;             ///< Bounding radius in pixels.
        cv::RotatedRect ellipse;    ///< Fitted ellipse from contour detection.
        bool hasEllipse = false;    ///< True if an ellipse was successfully fitted.
        cv::Mat H;                  ///< 3×3 perspective homography (ellipse → circle).
        cv::Size outSize;           ///< Output size of the rectified (warped) image.
    };

    /**
     * @struct ScaleCalibration
     * @brief Angle-based scale calibration data for circular gauges.
     *
     * Maps needle angle to gauge value using start/end angles and
     * the calibrated min/max values.
     */
    struct ScaleCalibration {
        double start_angle = 0;   ///< Angle (radians) of the minimum calibration marker.
        double end_angle = 0;     ///< Angle (radians) of the maximum calibration marker.
        double min_value = 0;     ///< Value at the minimum marker.
        double max_value = 1000;  ///< Value at the maximum marker.
        bool valid = false;       ///< True once calibration angles have been computed.
    };

    /**
     * @brief Default constructor.
     */
    CircularGauge() noexcept = default;

    /**
     * @brief Constructs a circular gauge with a center, radius, and color.
     * @param center  Center point of the gauge face.
     * @param radius  Bounding radius in pixels.
     * @param color   Drawing color (BGR).
     */
    CircularGauge(const cv::Point& center, int radius, const cv::Scalar& color);

    // ─── Static: Find all gauges in frame ─────────────────────────

    /**
     * @brief Detects all circular gauges in the given frame.
     *
     * Uses Canny edge detection, contour finding, and ellipse fitting.
     * Deduplicates overlapping detections and returns at most kMaxCirclesToKeep.
     * @param frame                BGR input image.
     * @param cannyThreshold       Canny edge detection high threshold.
     * @param accumulatorThreshold Accumulator threshold (unused, reserved).
     * @return Vector of ROI structs for detected gauges.
     */
    static std::vector<ROI> FindGauges(const cv::Mat& frame,
                                       int cannyThreshold,
                                       int accumulatorThreshold);

    // ─── Ellipse-to-Circle Homography ─────────────────────────────

    /**
     * @brief Computes an ellipse-to-circle homography from a set of edge points.
     *
     * Fits an ellipse to the points using cv::fitEllipse, then builds a
     * perspective transform that maps the ellipse to a circle.
     * @param pts              Edge points on the gauge perimeter.
     * @param[out] H           Output 3×3 homography matrix.
     * @param[out] outSize     Output size of the rectified image.
     * @param[out] ellipseRect Fitted rotated rectangle.
     * @param[out] inferredCenter Inferred center of the ellipse.
     * @return True on success, false if the fit failed.
     */
    static bool ComputeHomography(const std::vector<cv::Point>& pts,
                                  cv::Mat& H, cv::Size& outSize,
                                  cv::RotatedRect& ellipseRect,
                                  cv::Point& inferredCenter);

    /**
     * @brief Computes an ellipse-to-circle homography from a RotatedRect.
     * @param rr               Fitted rotated rectangle (ellipse).
     * @param[out] H           Output 3×3 homography matrix.
     * @param[out] outSize     Output size of the rectified image.
     * @param[out] inferredCenter Inferred center of the ellipse.
     * @return True on success.
     */
    static bool HomographyFromEllipse(const cv::RotatedRect& rr,
                                      cv::Mat& H, cv::Size& outSize,
                                      cv::Point& inferredCenter);

    /**
     * @brief Stores the homography and derived warp parameters.
     * @param H           3×3 perspective homography matrix.
     * @param outSize     Output size of the rectified image.
     * @param center      Center of the gauge in the original frame.
     * @param ellipseRect The fitted ellipse (default: empty).
     */
    void SetHomography(const cv::Mat& H, const cv::Size& outSize,
                       cv::Point center, cv::RotatedRect ellipseRect = {});

    /**
     * @brief Warps the input frame using the stored homography.
     * @param frame  BGR input image.
     * @return Warped (rectified) image, or the original frame if no homography.
     */
    cv::Mat WarpFrame(const cv::Mat& frame) const;

    /**
     * @brief Returns whether a homography has been computed.
     * @return True if the gauge has an active homography.
     */
    bool has_homography() const { return hasHomography_; }

    /**
     * @brief Returns the stored homography matrix.
     * @return Const reference to the 3×3 homography.
     */
    const cv::Mat& homography() const { return homography_; }

    /**
     * @brief Returns the output size of the rectified image.
     * @return Const reference to the warp output size.
     */
    const cv::Size& warp_size() const { return warpSize_; }

    // ─── Gauge interface overrides ────────────────────────────────

    /// @copydoc Gauge::InitMotionFeatures
    void InitMotionFeatures(const cv::Mat& frame) override;

    /// @copydoc Gauge::UpdateROI
    void UpdateROI(const cv::Mat& frame) override;

    /// @copydoc Gauge::DetectNeedle
    std::optional<double> DetectNeedle(const cv::Mat& frame) override;

    /// @copydoc Gauge::FinalizeCalibration
    void FinalizeCalibration() override;

    /// @copydoc Gauge::SetCalibrationValues
    void SetCalibrationValues(double minVal, double maxVal) override;

    /// @copydoc Gauge::DrawOverlay
    void DrawOverlay(cv::Mat& frame, int labelY = 60) const override;

    /// @copydoc Gauge::DrawCalibrationOverlay
    void DrawCalibrationOverlay(cv::Mat& frame) override;

    /// @copydoc Gauge::DrawOutline
    void DrawOutline(cv::Mat& img) const override;

    /// @copydoc Gauge::HandleClick
    CalibrationMarker HandleClick(int clickX, int clickY) override;

    /// @copydoc Gauge::MoveMarker
    void MoveMarker(CalibrationMarker which, cv::Point click) override;

    /// @copydoc Gauge::ResetMotionState
    void ResetMotionState() override;

    // ─── Circular-specific: angle-to-value ────────────────────────

    /**
     * @brief Converts a needle angle (radians) to a gauge value.
     *
     * Uses the stored ScaleCalibration start/end angles and min/max values.
     * @param needleAngle  Angle in radians (0 = right, π/2 = down).
     * @return Gauge value, or std::nullopt if the angle is outside the scale.
     */
    std::optional<double> AngleToValue(double needleAngle) const;

    // ─── Circular-specific: manual placement ──────────────────────

    /// Number of clicks required for manual placement.
    static constexpr int kManualClicks = 5;

    /**
     * @brief Returns the instruction string for a given manual-placement stage.
     * @param stage  0-based stage index (0..4).
     * @return Human-readable instruction C-string.
     */
    static const char* manualInstruction(int stage);

    /**
     * @brief Fits a circular gauge from manually clicked edge points.
     *
     * Calls ComputeHomography internally to fit an ellipse and derive the
     * perspective transform.
     * @param edges  Vector of user-clicked edge points (at least kManualClicks).
     * @return ROI on success, or std::nullopt if the fit failed.
     */
    static std::optional<ROI> FitFromManualEdges(
        const std::vector<cv::Point>& edges);

    // ─── Circular-specific: scale access ──────────────────────────

    /**
     * @brief Returns the angle-based scale calibration data.
     * @return Const reference to the ScaleCalibration struct.
     */
    const ScaleCalibration& scale() const { return scale_; }

private:
    /**
     * @struct RadialScanResult
     * @brief Result of scanning a single radial line for needle detection.
     */
    struct RadialScanResult {
        double density; ///< Fraction of dark pixels along the scan line.
        double reach;   ///< Longest contiguous dark run as fraction of radius.
    };

    /**
     * @brief Shared eigendecomposition + homography construction.
     *
     * Used by both ComputeHomography and HomographyFromEllipse.
     * Only the output radius R differs between the two callers.
     * @param cx      Ellipse center X.
     * @param cy      Ellipse center Y.
     * @param a       Semi-major axis length.
     * @param b       Semi-minor axis length.
     * @param theta   Rotation angle in radians.
     * @param R       Desired output circle radius.
     * @param[out] H  Output 3×3 homography.
     * @param[out] outSize  Output image size.
     * @return True on success.
     */
    static bool buildHomographyFromEllipse(float cx, float cy,
                                           float a, float b,
                                           double theta, double R,
                                           cv::Mat& H, cv::Size& outSize);

    ScaleCalibration scale_; ///< Angle-based scale calibration data.

    /**
     * @brief Creates a circular binary mask for the gauge face.
     * @param frame  Input BGR image (used for size).
     * @return Single-channel mask with the gauge interior set to 255.
     */
    cv::Mat CreateMask(const cv::Mat& frame) const;

    /**
     * @brief Returns the center point used during needle detection.
     *
     * When a homography is active, returns the rectangle center;
     * otherwise returns roi_.center.
     * @return Detection-space center point.
     */
    cv::Point detectionCenter() const;

    /**
     * @brief Detects a coloured (red) needle via HSV segmentation.
     * @param frame  BGR input image (masked to gauge face).
     * @return Needle angle in radians, or negative on failure.
     */
    double DetectColoredNeedle(const cv::Mat& frame) const;

    /**
     * @brief Detects a dark needle via adaptive thresholding and radial scan.
     * @param frame  BGR input image (masked to gauge face).
     * @return Needle angle in radians, or negative on failure.
     */
    double DetectNeedleRadial(const cv::Mat& frame) const;

    /**
     * @brief Scans a single radial line for needle detection.
     * @param binary  Adaptive-threshold binary image.
     * @param mask    Circular mask restricting the scan area.
     * @param angle   Scan angle in radians.
     * @return RadialScanResult with density and reach metrics.
     */
    RadialScanResult ScanRadialLine(const cv::Mat& binary,
                                    const cv::Mat& mask, double angle) const;

    double angle_ = -1; ///< Last detected needle angle (radians), or -1 if unknown.

    // Ellipse-to-circle homography state
    cv::Mat homography_;         ///< Active homography matrix (may be updated by motion compensation).
    cv::Mat homographyBase_;     ///< Original homography before motion compensation adjustments.
    cv::Size warpSize_;          ///< Output size of the rectified image.
    cv::Point2f rectCenter_;     ///< Center of the rectified circle.
    cv::RotatedRect ellipseRect_; ///< The fitted ellipse rectangle.
    bool hasHomography_ = false; ///< True when a valid homography is loaded.
};
