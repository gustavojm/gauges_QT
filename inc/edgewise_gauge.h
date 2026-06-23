#pragma once

#include "gauge.h"

#include <optional>

// ─── Edgewise (Panel Meter) Constants ────────────────────────────

// Detection: bezel finding
inline constexpr int kEdgewiseBlurKernel = 5;
inline constexpr double kEdgewiseMinArea = 2000.0;
inline constexpr double kEdgewiseMinAspect = 2.0;
inline constexpr double kEdgewiseMaxAspect = 5.0;
inline constexpr double kEdgewiseDuplicateDistFactor = 0.3;
inline constexpr size_t kEdgewiseMaxToKeep = 5;

// Detection: scale strip (white area)
inline constexpr int kEdgewiseScaleLowV = 180;
inline constexpr int kEdgewiseScaleMaxSat = 40;

// Detection: needle
inline constexpr int kEdgewiseMorphKernel = 3;
inline constexpr double kEdgewiseMinNeedleLength = 0.15;

// Calibration markers
inline constexpr int kEdgewiseMarkerRadius = 8;
inline constexpr int kEdgewiseMarkerOutlineRadius = 12;
inline constexpr double kEdgewiseHitThresh = 15.0;

// ─── Orientation ─────────────────────────────────────────────────

enum class InstrumentOrientation { kHorizontal, kVertical };

// ─── EdgewiseGauge ───────────────────────────────────────────────

class EdgewiseGauge : public Gauge {

public:
    EdgewiseGauge() noexcept = default;
    EdgewiseGauge(const cv::Rect& bezelRect, const cv::Scalar& color);

    // ─── Static: Find all edgewise gauges in frame ────────────────
    static std::vector<cv::Rect> FindGauges(const cv::Mat& frame,
                                             int cannyThreshold);

    // ─── Gauge interface overrides ────────────────────────────────
    void InitMotionFeatures(const cv::Mat& frame) override;
    void UpdateROI(const cv::Mat& frame) override;
    std::optional<double> DetectNeedle(const cv::Mat& frame) override;
    void FinalizeCalibration() override;
    void DrawOverlay(cv::Mat& frame, int labelY = 60) override;
    void DrawCalibrationOverlay(cv::Mat& frame) override;
    void DrawOutline(cv::Mat& img) const override;
    int  HandleClick(int clickX, int clickY) override;
    void MoveMarker(int which, cv::Point click) override;
    void ResetMotionState() override;

    // ─── Accessors ────────────────────────────────────────────────
    const cv::Rect& bezelRect() const { return bezelRect_; }
    InstrumentOrientation orientation() const { return orientation_; }

private:
    // Bezel geometry
    cv::Rect bezelRect_;
    cv::Rect bezelRectRef_;

    // Detected scale strip within the bezel
    cv::Rect scaleStrip_;

    // Orientation derived from bezel aspect ratio
    InstrumentOrientation orientation_ = InstrumentOrientation::kHorizontal;

    // Internal helpers
    cv::Rect detectScaleStrip(const cv::Mat& roiColor) const;
    std::optional<double> detectNeedlePosition(const cv::Mat& roiColor) const;
    std::optional<double> detectRedNeedle(const cv::Mat& roiHsv) const;
    std::optional<double> detectDarkNeedle(const cv::Mat& roiGray) const;
    double positionToValue(double needlePos) const;
};
