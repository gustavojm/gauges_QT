#include "gauge.h"

#include <algorithm>
#include <numeric>

int Gauge::next_number_ = 1;

// ═══════════════════════════════════════════════════════════════════
//  Construction
// ═══════════════════════════════════════════════════════════════════

Gauge::Gauge(const cv::Point& center, int radius, const cv::Scalar& color)
    : roi_({center, radius})
    , roi_ref_({center, radius})
    , color_(color)
    , number_(next_number_++)
{
    pt_min_ = center;
    pt_max_ = center;
}

// ═══════════════════════════════════════════════════════════════════
//  Color Palette
// ═══════════════════════════════════════════════════════════════════

cv::Scalar Gauge::NextColor() {
    static const std::vector<cv::Scalar> palette = {
        {0, 0, 255},      // red
        {255, 0, 0},      // blue
        {0, 255, 255},    // yellow
        {255, 0, 255},    // magenta
        {255, 255, 0},    // cyan
        {0, 165, 255},    // orange
        {128, 0, 128},    // purple
        {203, 192, 255},  // pink
        {0, 255, 0},      // green
    };
    static size_t idx = 0;
    return palette[idx++ % palette.size()];
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration
// ═══════════════════════════════════════════════════════════════════

void Gauge::SetCalibrationValues(double minVal, double maxVal) {
    min_value_ = minVal;
    max_value_ = maxVal;
    calib_values_set_ = true;
}

// ═══════════════════════════════════════════════════════════════════
//  Smoothing
// ═══════════════════════════════════════════════════════════════════

void Gauge::AddReading(std::optional<double> reading) {
    if (reading.has_value()) {
        readings_history_.push_back(*reading);
        if (readings_history_.size() > smooth_window_) readings_history_.pop_front();
    } else {
        if (!readings_history_.empty()) {
            readings_history_.pop_front();
        }
    }

    if (!readings_history_.empty()) {
        smoothed_reading_ =
            std::accumulate(readings_history_.begin(), readings_history_.end(), 0.0) /
            readings_history_.size();
    } else {
        smoothed_reading_ = std::nullopt;
    }
}

void Gauge::ResetSmoothing() {
    readings_history_.clear();
    smoothed_reading_ = std::nullopt;
}

void Gauge::ResetMotionState() {
    roi_ = roi_ref_;
}

// ═══════════════════════════════════════════════════════════════════
//  Drawing
// ═══════════════════════════════════════════════════════════════════

void Gauge::DrawGaugeNumber(cv::Mat& img) const {
    cv::putText(img, std::to_string(number_),
                roi_.center - cv::Point(8, 8),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, color_, 2);
}
