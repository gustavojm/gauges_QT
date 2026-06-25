#pragma once

#include <deque>
#include <memory>
#include <opencv2/opencv.hpp>
#include <optional>
#include <string>

// ─── Shared Types ─────────────────────────────────────────────────

/**
 * @brief Application operating mode.
 *
 * Drives which UI page is visible and which Worker logic is active.
 */
enum class AppMode {
    kDetection,   ///< Automatic or manual gauge detection from the first frame.
    kCalibration, ///< User adjusts min/max markers and alarm settings.
    kProcessing   ///< Frame-by-frame needle tracking with live values.
};

/**
 * @brief Gauge shape / instrument family.
 *
 * Selects the detection algorithm and the Gauge subclass instantiated.
 */
enum class GaugeType {
    kCircular,  ///< Round dial gauge with radial needle.
    kEdgewise   ///< Rectangular panel meter with linear scale.
};

/**
 * @brief Alarm comparison direction.
 *
 * Determines whether the alarm triggers when the reading falls below
 * or rises above the configured threshold.
 */
enum class AlarmDirection {
    kLessThan,   ///< Trigger when reading < threshold.
    kGreaterThan ///< Trigger when reading > threshold.
};

// ─── Named constants ─────────────────────────────────────────────

/// Mathematical constant π.
inline constexpr double kPi = 3.14159265358979323846;

// Overlay drawing
inline constexpr int kCircleThickness = 2;          ///< Line thickness for gauge circle outline.
inline constexpr int kNeedleThickness = 3;          ///< Line thickness for the drawn needle.
inline constexpr double kNeedleLengthFactor = 0.8;  ///< Needle length as fraction of radius.
inline constexpr int kCenterDotRadius = 5;          ///< Radius of the center dot overlay.
inline constexpr int kCalibPtRadius = 10;           ///< Radius of calibration marker fill.
inline constexpr int kCalibPtOutlineRadius = 14;    ///< Radius of calibration marker outline.
inline constexpr int kCalibCenterDotRadius = 4;     ///< Radius of calibration center dot.
inline constexpr double kHitTestMinThresh = 12.0;   ///< Minimum pixel distance for marker hit test.

/**
 * @brief Calibration marker identifier.
 *
 * Used to track which calibration marker is being dragged or hit during
 * mouse interactions in calibration mode.
 */
enum class CalibrationMarker {
    kNone = -1, ///< No marker hit.
    kMin = 0,   ///< Minimum calibration marker.
    kMax = 1    ///< Maximum calibration marker.
};

// ─── Gauge Base Class ────────────────────────────────────────────

/**
 * @class Gauge
 * @brief Abstract base class for all gauge types (circular, edgewise).
 *
 * Provides shared state for calibration markers, smoothing, alarm logic,
 * motion compensation, and drawing. Shape-specific behaviour is delegated
 * to pure virtual methods overridden by CircularGauge and EdgewiseGauge.
 *
 * @see CircularGauge
 * @see EdgewiseGauge
 */
class Gauge {

public:
    /**
     * @struct ROI
     * @brief Simple region of interest — center + bounding radius.
     *
     * Subclasses may extend with shape-specific geometry (e.g. ellipse).
     */
    struct ROI {
        cv::Point center; ///< Center point of the region.
        int radius = 0;   ///< Bounding radius in pixels.
    };

    /**
     * @brief Default constructor.
     *
     * Creates an uninitialized gauge with default ROI and color.
     */
    Gauge() noexcept = default;

    /**
     * @brief Constructs a gauge at the given center with a bounding radius and color.
     * @param center  Center point of the gauge face in the image.
     * @param radius  Bounding radius of the gauge face in pixels.
     * @param color   Drawing color for this gauge (BGR).
     */
    Gauge(const cv::Point& center, int radius, const cv::Scalar& color);

    /**
     * @brief Virtual destructor for safe polymorphic deletion.
     */
    virtual ~Gauge() = default;

    /**
     * @brief Returns whether this gauge was placed manually.
     * @return True if the user placed this gauge by clicking edge points.
     */
    bool is_manual() const { return is_manual_; }

    /**
     * @brief Marks the gauge as manually or automatically placed.
     * @param manual  True to mark as manually placed.
     */
    void set_manual(bool manual) { is_manual_ = manual; }

    // ─── Tag ──────────────────────────────────────────────────────

    /**
     * @brief Sets the user-defined tag (instrument identifier) for this gauge.
     * @param tag  Tag string, e.g. "64323-PI-165".
     */
    void set_tag(const std::string& tag) { tag_ = tag; }

    /**
     * @brief Returns the user-defined tag for this gauge.
     * @return Const reference to the tag string.
     */
    const std::string& tag() const { return tag_; }

    // ─── Static: Color palette ────────────────────────────────────

    /**
     * @brief Returns the next color from the rotating palette.
     *
     * Colors cycle through a predefined set of distinguishable BGR values.
     * @return BGR scalar for drawing.
     */
    static cv::Scalar NextColor();

    // ─── Calibration (generic) ────────────────────────────────────

    /**
     * @brief Sets the calibration min/max value range.
     * @param minVal  Value at the minimum calibration marker.
     * @param maxVal  Value at the maximum calibration marker.
     */
    virtual void SetCalibrationValues(double minVal, double maxVal);

    /**
     * @brief Returns the calibrated minimum value.
     * @return Minimum value of the gauge scale.
     */
    double min_value() const { return min_value_; }

    /**
     * @brief Returns the calibrated maximum value.
     * @return Maximum value of the gauge scale.
     */
    double max_value() const { return max_value_; }

    // ─── Smoothing ────────────────────────────────────────────────

    /**
     * @brief Adds a new raw reading to the smoothing window.
     * @param reading  Raw needle value, or std::nullopt if detection failed.
     */
    void AddReading(std::optional<double> reading);

    /**
     * @brief Returns the current smoothed (moving-average) value.
     * @return Smoothed value, or std::nullopt if no readings yet.
     */
    std::optional<double> smoothed_value() const { return smoothed_reading_; }

    /**
     * @brief Clears the smoothing history and resets the smoothed value.
     */
    void ResetSmoothing();

    // ─── Alarm ────────────────────────────────────────────────────

    /**
     * @brief Enables or disables the alarm for this gauge.
     * @param enabled  True to enable.
     */
    void set_alarm_enabled(bool enabled) { alarm_enabled_ = enabled; }

    /**
     * @brief Sets the alarm comparison direction.
     * @param dir  AlarmDirection::kLessThan or kGreaterThan.
     */
    void set_alarm_direction(AlarmDirection dir) { alarm_direction_ = dir; }

    /**
     * @brief Sets the alarm threshold value.
     * @param threshold  Numeric threshold for alarm comparison.
     */
    void set_alarm_threshold(double threshold) { alarm_threshold_ = threshold; }

    /**
     * @brief Returns whether the alarm is enabled.
     * @return True if alarm monitoring is active.
     */
    bool alarm_enabled() const { return alarm_enabled_; }

    /**
     * @brief Returns the current alarm direction setting.
     * @return The AlarmDirection enum value.
     */
    AlarmDirection alarm_direction() const { return alarm_direction_; }

    /**
     * @brief Returns the current alarm threshold.
     * @return Numeric threshold.
     */
    double alarm_threshold() const { return alarm_threshold_; }

    /**
     * @brief Evaluates whether the alarm should trigger based on the current smoothed value.
     * @return True if the alarm condition is met.
     */
    bool CheckAlarm() const;

    // ─── Motion State Reset ───────────────────────────────────────

    /**
     * @brief Resets motion-compensation state to the initial reference frame.
     *
     * Called when the user clicks "Restart" during processing.
     */
    virtual void ResetMotionState();

    // ─── State Access ─────────────────────────────────────────────

    /**
     * @brief Returns the 1-based gauge identification number.
     * @return Gauge number.
     */
    int number() const { return number_; }

    /**
     * @brief Returns and increments the global gauge counter.
     * @return The next gauge number (1-based).
     */
    static int NextNumber() { next_number_++; return next_number_; }

    /**
     * @brief Resets the global gauge counter to 1 and returns it.
     * @return 1 (the reset value).
     */
    static int ResetNextNumber() { next_number_ = 1; return next_number_; }

    /**
     * @brief Manually sets the gauge identification number.
     * @param number  1-based gauge number.
     */
    void set_number(int number) {  number_ = number; }

    /**
     * @brief Returns the drawing color for this gauge.
     * @return Const reference to the BGR color scalar.
     */
    const cv::Scalar& color() const { return color_; }

    /**
     * @brief Returns the region of interest for this gauge.
     * @return Const reference to the ROI struct.
     */
    const ROI& roi() const { return roi_; }

    // ─── Drawing (generic) ────────────────────────────────────────

    /**
     * @brief Draws the gauge identification number on the frame.
     * @param img  Output image to draw on.
     */
    void DrawGaugeNumber(cv::Mat& img) const;

    /**
     * @brief Draws the current smoothed value as text on the frame.
     * @param frame  Output image to draw on.
     * @param labelY  Vertical pixel position for the text label.
     */
    void DrawValueText(cv::Mat& frame, int labelY) const;

    // ─── Pure virtual interface (shape-specific) ──────────────────

    /**
     * @brief Initializes motion-compensation features from the first processing frame.
     * @param frame  BGR input frame.
     */
    virtual void InitMotionFeatures(const cv::Mat& frame) = 0;

    /**
     * @brief Updates the ROI position using optical-flow motion compensation.
     * @param frame  Current BGR processing frame.
     */
    virtual void UpdateROI(const cv::Mat& frame) = 0;

    /**
     * @brief Detects the needle and returns the raw gauge reading.
     * @param frame  Current BGR processing frame.
     * @return Raw gauge value, or std::nullopt if detection failed.
     */
    virtual std::optional<double> DetectNeedle(const cv::Mat& frame) = 0;

    /**
     * @brief Finalizes calibration after the user confirms marker positions.
     *
     * Subclasses compute shape-specific calibration data (e.g. angle ranges).
     */
    virtual void FinalizeCalibration() = 0;

    /**
     * @brief Draws the processing-mode overlay (circle/ellipse, needle, value).
     * @param frame   Output image to draw on.
     * @param labelY  Vertical pixel position for the value text label.
     */
    virtual void DrawOverlay(cv::Mat& frame, int labelY = 60) const = 0;

    /**
     * @brief Draws the calibration-mode overlay (markers, arc, labels).
     * @param frame  Output image to draw on.
     */
    virtual void DrawCalibrationOverlay(cv::Mat& frame) = 0;

    /**
     * @brief Draws the gauge outline (detection mode).
     * @param img  Output image to draw on.
     */
    virtual void DrawOutline(cv::Mat& img) const = 0;

    /**
     * @brief Handles a mouse click for calibration marker hit-testing.
     * @param clickX  X coordinate of the click in image space.
     * @param clickY  Y coordinate of the click in image space.
     * @return CalibrationMarker::kMin, CalibrationMarker::kMax, or CalibrationMarker::kNone.
     */
    virtual CalibrationMarker HandleClick(int clickX, int clickY) = 0;

    /**
     * @brief Moves a calibration marker to a new position.
     * @param which  CalibrationMarker::kMin or CalibrationMarker::kMax.
     * @param click  New position in image space.
     */
    virtual void MoveMarker(CalibrationMarker which, cv::Point click) = 0;

protected:
    // ROI (center + bounding radius)
    ROI roi_ = {};         ///< Current region of interest (updated by motion compensation).
    ROI roi_ref_ = {};     ///< Reference ROI from the first processing frame.

    // Color
    cv::Scalar color_ = {0, 255, 0}; ///< Drawing colour (BGR).

    // Calibration markers
    cv::Point pt_min_;             ///< Minimum calibration marker position.
    cv::Point pt_max_;             ///< Maximum calibration marker position.
    double min_value_ = 0;         ///< Calibrated minimum value.
    double max_value_ = 1000;      ///< Calibrated maximum value.
    bool calib_values_set_ = false; ///< True once calibration values have been applied.

    // Smoothing
    std::deque<double> readings_history_;   ///< Raw reading history for moving average.
    size_t smooth_window_ = 5;             ///< Number of readings in the smoothing window.
    std::optional<double> last_reading_ = std::nullopt;     ///< Most recent raw reading.
    std::optional<double> smoothed_reading_ = std::nullopt; ///< Current smoothed value.

    // Alarm
    bool alarm_enabled_ = false;                          ///< Alarm active flag.
    AlarmDirection alarm_direction_ = AlarmDirection::kGreaterThan; ///< Alarm comparison direction.
    double alarm_threshold_ = 0;                          ///< Alarm threshold value.

    // Gauge identification
    int number_ = 0;                      ///< 1-based gauge number.
    static int next_number_;              ///< Global counter for assigning gauge numbers.
    bool is_manual_ = false;              ///< True if placed manually by the user.
    std::string tag_;                     ///< User-defined instrument tag.

    // Motion compensation state
    cv::Mat ref_gray_;                        ///< Grayscale reference frame.
    std::vector<cv::Point2f> ref_features_;   ///< Features detected in the reference frame.
    cv::Mat prev_gray_;                       ///< Previous frame grayscale (for optical flow).
    std::vector<cv::Point2f> prev_features_;  ///< Features tracked from the previous frame.
};

// ─── Motion compensation constants (used by all gauge types) ─────

inline constexpr int kMaxFeatures = 100;               ///< Maximum number of features to track.
inline constexpr double kFeatureQuality = 0.01;         ///< Minimum corner quality for feature detection.
inline constexpr int kFeatureBlockSize = 7;             ///< Block size for the feature detector.
inline constexpr double kMinFeatureDist = 5.0;          ///< Minimum pixel distance between features.
inline constexpr size_t kMinPointsForTransform = 6;     ///< Minimum tracked points for affine estimation.
inline constexpr double kInlierReprojThresh = 3.0;      ///< RANSAC inlier reprojection threshold.

// ─── Qt metatype ─────────────────────────────────────────────────
// Note: Q_DECLARE_METATYPE(AppMode) is in circular_gauge.h where QObject is included.
