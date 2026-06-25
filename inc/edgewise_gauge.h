#pragma once

#include "gauge.h"

#include <optional>

// ─── Edgewise (Panel Meter) Constants ────────────────────────────

// Detection: bezel finding
inline constexpr int kEdgewiseBlurKernel = 5;             ///< Gaussian blur kernel size for edge detection.
inline constexpr double kEdgewiseMinArea = 2000.0;        ///< Minimum contour area to consider as a bezel.
inline constexpr double kEdgewiseMinAspect = 2.0;         ///< Minimum width/height aspect ratio.
inline constexpr double kEdgewiseMaxAspect = 5.0;         ///< Maximum width/height aspect ratio.
inline constexpr double kEdgewiseDuplicateDistFactor = 0.3; ///< Deduplication distance as fraction of bezel size.
inline constexpr size_t kEdgewiseMaxToKeep = 5;           ///< Maximum number of edgewise gauges to keep.

// Detection: scale strip (white area)
inline constexpr int kEdgewiseScaleLowV = 180;            ///< Minimum value (brightness) for scale strip.
inline constexpr int kEdgewiseScaleMaxSat = 40;           ///< Maximum saturation for scale strip (white).

// Detection: needle
inline constexpr int kEdgewiseMorphKernel = 3;            ///< Morphological operation kernel size.
inline constexpr double kEdgewiseMinNeedleLength = 0.15;  ///< Minimum needle length as fraction of scale strip.

// Calibration markers
inline constexpr int kEdgewiseMarkerRadius = 8;           ///< Radius of calibration marker fill.
inline constexpr int kEdgewiseMarkerOutlineRadius = 12;   ///< Radius of calibration marker outline.
inline constexpr double kEdgewiseHitThresh = 15.0;        ///< Pixel distance threshold for marker hit test.

// ─── Orientation ─────────────────────────────────────────────────

/**
 * @brief Physical orientation of an edgewise (panel meter) instrument.
 *
 * Determines whether the needle moves horizontally or vertically
 * and which axis is used for value mapping.
 */
enum class InstrumentOrientation {
    kHorizontal, ///< Scale runs left-to-right.
    kVertical    ///< Scale runs top-to-bottom.
};

// ─── EdgewiseGauge ───────────────────────────────────────────────

/**
 * @class EdgewiseGauge
 * @brief Gauge subclass for rectangular panel meters (edgewise instruments).
 *
 * Detects rectangular bezels via contour approximation, locates the white
 * scale strip within the bezel, and tracks the needle using either red
 * colour segmentation or Hough line detection for dark needles.
 *
 * @see Gauge
 * @see CircularGauge
 */
class EdgewiseGauge : public Gauge {

public:
    /**
     * @brief Default constructor.
     */
    EdgewiseGauge() noexcept = default;

    /**
     * @brief Constructs an edgewise gauge from a bezel rectangle and color.
     * @param bezelRect  Bounding rectangle of the instrument bezel.
     * @param color      Drawing color (BGR).
     */
    EdgewiseGauge(const cv::Rect& bezelRect, const cv::Scalar& color);

    // ─── Static: Find all edgewise gauges in frame ────────────────

    /**
     * @brief Detects all rectangular (edgewise) gauges in the given frame.
     *
     * Uses Canny edge detection, contour approximation to quadrilaterals,
     * and aspect-ratio filtering to identify panel meters.
     * @param frame           BGR input image.
     * @param cannyThreshold  Canny edge detection high threshold.
     * @return Vector of bounding rectangles for detected gauges.
     */
    static std::vector<cv::Rect> FindGauges(const cv::Mat& frame,
                                             int cannyThreshold);

    // ─── Gauge interface overrides ────────────────────────────────

    /// @copydoc Gauge::InitMotionFeatures
    void InitMotionFeatures(const cv::Mat& frame) override;

    /// @copydoc Gauge::UpdateROI
    void UpdateROI(const cv::Mat& frame) override;

    /// @copydoc Gauge::DetectNeedle
    std::optional<double> DetectNeedle(const cv::Mat& frame) override;

    /// @copydoc Gauge::FinalizeCalibration
    void FinalizeCalibration() override;

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

    // ─── Accessors ────────────────────────────────────────────────

    /**
     * @brief Returns the bezel bounding rectangle.
     * @return Const reference to the cv::Rect.
     */
    const cv::Rect& bezel_rect() const { return bezelRect_; }

    /**
     * @brief Returns the detected instrument orientation.
     * @return InstrumentOrientation::kHorizontal or kVertical.
     */
    InstrumentOrientation orientation() const { return orientation_; }

    // ─── Edgewise-specific: manual placement ──────────────────────

    /// Number of clicks required for manual placement (4 corners).
    static constexpr int kManualClicks = 4;

    /**
     * @brief Returns the instruction string for a given manual-placement stage.
     * @param stage  0-based stage index (0..3).
     * @return Human-readable instruction C-string.
     */
    static const char* manualInstruction(int stage);

    /**
     * @brief Fits an edgewise gauge from manually clicked corner points.
     * @param edges  Vector of user-clicked corner points (at least kManualClicks).
     * @return Bounding rectangle on success, or std::nullopt if too few points.
     */
    static std::optional<cv::Rect> FitFromManualEdges(
        const std::vector<cv::Point>& edges);

private:
    cv::Rect bezelRect_;     ///< Current bezel bounding rectangle (updated by motion compensation).
    cv::Rect bezelRectRef_;  ///< Reference bezel rectangle from the first processing frame.

    cv::Rect scaleStrip_;    ///< Detected white scale strip within the bezel.

    InstrumentOrientation orientation_ = InstrumentOrientation::kHorizontal; ///< Instrument orientation.

    /**
     * @brief Detects the white scale strip within the bezel ROI.
     * @param roiColor  BGR colour image of the bezel interior.
     * @return Bounding rectangle of the scale strip (relative to ROI).
     */
    cv::Rect detectScaleStrip(const cv::Mat& roiColor) const;

    /**
     * @brief Detects the needle position within the bezel ROI.
     *
     * Tries red needle detection first, then falls back to dark needle detection.
     * @param roiColor  BGR colour image of the bezel interior.
     * @return Needle position along the scale axis, or std::nullopt.
     */
    std::optional<double> detectNeedlePosition(const cv::Mat& roiColor) const;

    /**
     * @brief Detects a red needle via HSV colour segmentation.
     * @param roiHsv  HSV colour image of the bezel interior.
     * @return Needle centroid position along the scale axis, or std::nullopt.
     */
    std::optional<double> detectRedNeedle(const cv::Mat& roiHsv) const;

    /**
     * @brief Detects a dark needle via Hough line detection.
     * @param roiGray  Grayscale image of the bezel interior.
     * @return Needle position along the scale axis, or std::nullopt.
     */
    std::optional<double> detectDarkNeedle(const cv::Mat& roiGray) const;

    /**
     * @brief Maps a needle pixel position to a gauge value.
     * @param needlePos  Needle position along the scale axis (pixels).
     * @return Mapped gauge value within [min_value_, max_value_].
     */
    double positionToValue(double needlePos) const;
};
