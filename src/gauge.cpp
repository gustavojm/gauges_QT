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

void Gauge::AddReading(double value) {
    if (value >= 0) {
        value_history_.push_back(value);
        if (value_history_.size() > smooth_window_) value_history_.pop_front();
    }

    smoothed_value_ = value;
    if (!value_history_.empty()) {
        smoothed_value_ =
            std::accumulate(value_history_.begin(), value_history_.end(), 0.0) /
            value_history_.size();
    }
}

void Gauge::ResetSmoothing() {
    value_history_.clear();
    smoothed_value_ = 0;
}

// ═══════════════════════════════════════════════════════════════════
//  Drawing
// ═══════════════════════════════════════════════════════════════════

void Gauge::DrawGaugeNumber(cv::Mat& img) const {
    cv::putText(img, std::to_string(number_),
                roi_.center - cv::Point(8, 8),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, color_, 2);
}
