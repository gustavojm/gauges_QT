#include "circular_gauge.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <sstream>
#include <string>

constexpr int kCircleThickness = 2;
constexpr int kNeedleThickness = 3;
constexpr int kCalibPtRadius = 10;

int CircularGauge::next_number_ = 1;

void CircularGauge::StartProcessing() {
    state_ = GaugeState::kProcessing;
}

// ═══════════════════════════════════════════════════════════════════
//  Static: Find all gauges in frame
// ═══════════════════════════════════════════════════════════════════

std::vector<CircularGauge::ROI> CircularGauge::FindGauges(const cv::Mat& frame,
                                                int cannyThreshold,
                                                int accumulatorThreshold) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    int maxDim = std::max(gray.rows, gray.cols);
    int minR = maxDim / 15;
    int maxR = maxDim / 2;

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);
    std::vector<cv::Vec3f> allCircles;
    cv::HoughCircles(blurred, allCircles, cv::HOUGH_GRADIENT, 1.0,
                     gray.rows / 6, cannyThreshold, accumulatorThreshold, minR,
                     maxR);

    constexpr size_t maxCirclesToKeep = 5;
    if (allCircles.size() > maxCirclesToKeep)
        allCircles.resize(maxCirclesToKeep);

    std::vector<CircularGauge::ROI> result;
    for (const auto& c : allCircles) {
        cv::Point center(cvRound(c[0]), cvRound(c[1]));
        int radius = cvRound(c[2]);
        bool duplicate = false;
        for (const auto& existing : result) {
            if (cv::norm(center - existing.center) < existing.radius * 0.3) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            result.push_back({center, radius});
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════
//  Mask
// ═══════════════════════════════════════════════════════════════════

cv::Mat CircularGauge::CreateMask(const cv::Mat& frame) const {
    cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
    cv::circle(mask, roi_.center, roi_.radius, cv::Scalar(255), -1);
    return mask;   
}

// ═══════════════════════════════════════════════════════════════════
//  Coloured Needle Detection
// ═══════════════════════════════════════════════════════════════════

double CircularGauge::DetectColoredNeedle(const cv::Mat& frame) const {
    cv::Mat mask = CreateMask(frame);
    cv::Mat masked;
    frame.copyTo(masked, mask);

    cv::Mat hsv;
    cv::cvtColor(masked, hsv, cv::COLOR_BGR2HSV);

    cv::Mat red1, red2, redMask;
    cv::inRange(hsv, cv::Scalar(0, 50, 50), cv::Scalar(10, 255, 255), red1);
    cv::inRange(hsv, cv::Scalar(160, 50, 50), cv::Scalar(179, 255, 255), red2);
    cv::bitwise_or(red1, red2, redMask);

    cv::Mat kernel =
        cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(redMask, redMask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(redMask, redMask, cv::MORPH_OPEN, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(redMask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) return -1;

    int bestIdx = -1;
    double bestScore = 0;
    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area < roi_.radius * 0.5) continue;
        cv::Moments m = cv::moments(contours[i]);
        if (m.m00 == 0) continue;
        cv::Point centroid(cvRound(m.m10 / m.m00), cvRound(m.m01 / m.m00));
        if (cv::norm(centroid - roi_.center) > roi_.radius * 0.6) continue;
        double maxDist = 0;
        for (const auto& pt : contours[i])
            maxDist = std::max(maxDist, cv::norm(pt - roi_.center));
        double score = maxDist * std::log(area + 1);
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    if (bestIdx < 0) return -1;

    double maxDist = 0;
    cv::Point tip;
    for (const auto& pt : contours[bestIdx]) {
        double d = cv::norm(pt - roi_.center);
        if (d > maxDist) {
            maxDist = d;
            tip = pt;
        }
    }
    return std::atan2(tip.y - roi_.center.y, tip.x - roi_.center.x);
}

// ═══════════════════════════════════════════════════════════════════
//  Radial Needle Detection
// ═══════════════════════════════════════════════════════════════════

double CircularGauge::DetectNeedleRadial(const cv::Mat& frame) const {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::Mat mask = CreateMask(frame);
    cv::Mat masked;
    gray.copyTo(masked, mask);

    cv::Mat blurred;
    cv::GaussianBlur(masked, blurred, cv::Size(5, 5), 0);
    cv::Mat binary;
    cv::adaptiveThreshold(blurred, binary, 255, cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 25, 8);

    int numAngles = 360;
    double bestScore = 0;
    double bestAngle = -1;
    int startR = cvRound(roi_.radius * 0.08);
    int endR = roi_.radius;

    for (int i = 0; i < numAngles; i++) {
        double angle = 2.0 * kPi * i / numAngles;
        int longestRun = 0, darkRun = 0, totalDark = 0, totalScan = 0;
        bool inRun = false;
        for (int r = startR; r < endR; r++) {
            int x = cvRound(roi_.center.x + r * std::cos(angle));
            int y = cvRound(roi_.center.y + r * std::sin(angle));
            if (x < 0 || x >= binary.cols || y < 0 || y >= binary.rows) break;
            if (mask.at<uchar>(y, x) == 0) break;
            totalScan++;
            if (binary.at<uchar>(y, x) > 0) {
                totalDark++;
                if (!inRun) {
                    inRun = true;
                    darkRun = 1;
                } else
                    darkRun++;
                if (darkRun > longestRun) longestRun = darkRun;
            } else
                inRun = false;
        }
        if (inRun && darkRun > longestRun) longestRun = darkRun;
        double density = totalScan > 0 ? (double)totalDark / totalScan : 0;
        double reach = totalScan > 0 ? (double)longestRun / roi_.radius : 0;
        double score = density * 0.4 + reach * 0.6;
        if (score > bestScore) {
            bestScore = score;
            bestAngle = angle;
        }
    }
    return bestAngle;
}

double CircularGauge::SmoothReadings(double value) {
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
    return smoothed_value_;
}

double CircularGauge::DetectNeedle(const cv::Mat& frame) {
    angle_ = DetectColoredNeedle(frame);
    if (angle_ < 0) {
        angle_ = DetectNeedleRadial(frame);
    }

    if (angle_ >= 0) {
        value_ = AngleToValue(angle_);
        SmoothReadings(value_);
        return value_;
    }
    return -1;
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::FinalizeCalibration() {
    scale_.start_angle =
        std::atan2(pt_min_.y - roi_.center.y, pt_min_.x - roi_.center.x);
    scale_.end_angle =
        std::atan2(pt_max_.y - roi_.center.y, pt_max_.x - roi_.center.x);

    scale_.valid = true;
}

void CircularGauge::SetCalibrationValues(double minVal, double maxVal) {
    scale_.min_value = minVal;
    scale_.max_value = maxVal;
    scale_.valid = true;
}

// ═══════════════════════════════════════════════════════════════════
//  Angle-to-Value
// ═══════════════════════════════════════════════════════════════════

double CircularGauge::AngleToValue(double needleAngle) const {
    if (!scale_.valid || needleAngle < 0) return -1;
    auto normalize = [](double a) {
        a = std::fmod(a, 2.0 * kPi);
        return a < 0 ? a + 2.0 * kPi : a;
    };
    double start = normalize(scale_.start_angle);
    double end = normalize(scale_.end_angle);
    double needle = normalize(needleAngle);

    double range = (end > start) ? (end - start) : ((2.0 * kPi - start) + end);
    double pos;
    if (end > start) {
        if (needle >= start && needle <= end)
            pos = (needle - start) / range;
        else if (needle < start)
            pos = ((needle + 2.0 * kPi) - start) / range;
        else
            pos = (needle - start) / range;
    } else {
        if (needle >= start)
            pos = (needle - start) / range;
        else if (needle <= end)
            pos = ((needle + 2.0 * kPi) - start) / range;
        else
            return -1;
    }
    pos = std::clamp(pos, 0.0, 1.0);
    return scale_.min_value + pos * (scale_.max_value - scale_.min_value);
}

// ═══════════════════════════════════════════════════════════════════
//  Overlay Drawing
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::DrawOverlay(cv::Mat& frame, int labelY) const {
    if (roi_.radius > 0) {
        cv::circle(frame, roi_.center, roi_.radius, color_,
                   kCircleThickness);
        if (scale_.valid) {
            int arcR = cvRound(roi_.radius * kRadiusInset);
            cv::Point startPt(
                roi_.center.x + cvRound(arcR * std::cos(scale_.start_angle)),
                roi_.center.y + cvRound(arcR * std::sin(scale_.start_angle)));
            cv::line(frame, roi_.center, startPt, cv::Scalar(0, 255, 0), 2);
            cv::putText(frame, std::to_string((int)scale_.min_value),
                        startPt + cv::Point(10, 0), cv::FONT_HERSHEY_SIMPLEX,
                        0.6, cv::Scalar(0, 255, 0), 2);
            cv::Point endPt(
                roi_.center.x + cvRound(arcR * std::cos(scale_.end_angle)),
                roi_.center.y + cvRound(arcR * std::sin(scale_.end_angle)));
            cv::line(frame, roi_.center, endPt, cv::Scalar(0, 0, 255), 2);
            cv::putText(frame, std::to_string((int)scale_.max_value),
                        endPt + cv::Point(10, 0), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                        cv::Scalar(0, 0, 255), 2);
        }
        if (angle_ >= 0) {
            cv::Point needleTip(roi_.center.x + cvRound(roi_.radius * 0.8 *
                                                          std::cos(angle_)),
                                roi_.center.y + cvRound(roi_.radius * 0.8 *
                                                          std::sin(angle_)));
            cv::line(frame, roi_.center, needleTip, cv::Scalar(255, 0, 0), 3);
            cv::circle(frame, roi_.center, 5, color_, -1);
        }
    }

    DrawGaugeNumber(frame);

    std::ostringstream oss;
    oss << "Value: " << std::fixed << std::setprecision(2) << value_;
    cv::putText(frame, oss.str(), cv::Point(30, labelY),
                cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 255, 255), 3);
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Overlay Drawing
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::DrawCalibrationOverlay(cv::Mat& frame) const {
    if (state_ != GaugeState::kCalibrating) return;
    if (pt_min_ == cv::Point() && pt_max_ == cv::Point()) return;

    int thickness = 1;
    int arcR = cvRound(roi_.radius * kRadiusInset);

    cv::circle(frame, roi_.center, 4, cv::Scalar(255, 255, 255), -1);

    cv::Point vecMin = pt_min_ - roi_.center;
    cv::Point vecMax = pt_max_ - roi_.center;
    cv::Point ptMinIn = roi_.center + cv::Point(
        cvRound(vecMin.x * kRadiusInset), cvRound(vecMin.y * kRadiusInset));
    cv::Point ptMaxIn = roi_.center + cv::Point(
        cvRound(vecMax.x * kRadiusInset), cvRound(vecMax.y * kRadiusInset));

    double a0 = std::atan2(vecMin.y, vecMin.x) * 180.0 / kPi;
    double a1 = std::atan2(vecMax.y, vecMax.x) * 180.0 / kPi;
    if (a0 < 0) a0 += 360;
    if (a1 < 0) a1 += 360;
    {
        double cwEnd = (a1 > a0) ? a1 : a1 + 360;
        bool cwTop = (a0 <= 270 && 270 <= cwEnd);
        if (!cwTop) std::swap(a0, a1);
    }
    if (a1 <= a0) a1 += 360;
    cv::ellipse(frame, roi_.center, cv::Size(arcR, arcR), 0, a0, a1,
                cv::Scalar(0, 255, 255), thickness);

    cv::circle(frame, ptMinIn, 10, cv::Scalar(0, 255, 0), -1);
    cv::circle(frame, ptMinIn, 14, cv::Scalar(0, 255, 0), thickness);
    cv::putText(frame, "min", ptMinIn + cv::Point(12, 4),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

    cv::circle(frame, ptMaxIn, 10, cv::Scalar(0, 0, 255), -1);
    cv::circle(frame, ptMaxIn, 14, cv::Scalar(0, 0, 255), thickness);
    cv::putText(frame, "max", ptMaxIn + cv::Point(12, 4),
                cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);
}

double CircularGauge::GetSmoothedValue() const { return smoothed_value_; }

cv::Scalar CircularGauge::NextColor() {
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

int CircularGauge::HandleClick(int clickX, int clickY) {
    if (state_ != GaugeState::kCalibrating) return kMarkerNone;

    // kMarkerMin if click is near pt_min_, kMarkerMax if near pt_max_,
    // or kMarkerNone otherwise.  (threshold = radius/6)
    int thresh = std::max(roi_.radius / 6, 12);
    cv::Point center = roi_.center;
    cv::Point vecMin = pt_min_ - center;
    cv::Point vecMax = pt_max_ - center;
    cv::Point ptMinIn = center + cv::Point(
        cvRound(vecMin.x * kRadiusInset), cvRound(vecMin.y * kRadiusInset));
    cv::Point ptMaxIn = center + cv::Point(
        cvRound(vecMax.x * kRadiusInset), cvRound(vecMax.y * kRadiusInset));
    int dMin = cvRound(cv::norm(cv::Point(clickX, clickY) - ptMinIn));
    int dMax = cvRound(cv::norm(cv::Point(clickX, clickY) - ptMaxIn));
    int hit = kMarkerNone;
    if (dMin <= thresh && dMin <= dMax)
        hit = kMarkerMin;
    else if (dMax <= thresh)
        hit = kMarkerMax;
    if (hit != kMarkerNone) {
        MoveMarker(hit, cv::Point(clickX, clickY));
    }
    return hit;
}

// ═══════════════════════════════════════════════════════════════════
//  Static helpers
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::DrawGaugeNumber(cv::Mat& img) const {
    cv::putText(img, std::to_string(number_),
                roi_.center - cv::Point(8, 8),
                cv::FONT_HERSHEY_SIMPLEX, 0.8, color_, 2);
}

void CircularGauge::DrawOutline(cv::Mat& img) const {
    if (roi_.radius <= 0) return;
    cv::circle(img, roi_.center, roi_.radius, color_, 2);
    DrawGaugeNumber(img);
}

CircularGauge::CircularGauge(const cv::Point& center, int radius,
                             const cv::Scalar& color) {
    roi_ = {center, radius};
    color_ = color;
    state_ = GaugeState::kCalibrating;
    number_ = next_number_++;
    // Default markers at 135° (top-right) and 45° (top-left)
    double a = 3.0 * kPi / 4.0;
    pt_min_ = center + cv::Point(cvRound(radius * std::cos(a)),
                                 cvRound(radius * std::sin(a)));
    a = kPi / 4.0;
    pt_max_ = center + cv::Point(cvRound(radius * std::cos(a)),
                                 cvRound(radius * std::sin(a)));
}

void CircularGauge::MoveMarker(int which, cv::Point click) {
    if (state_ != GaugeState::kCalibrating) return;

    cv::Point vec = click - roi_.center;
    double dist = cv::norm(vec);
    if (dist < 1.0) return;
    double scale = static_cast<double>(roi_.radius) / dist;
    cv::Point onCircle = roi_.center + cv::Point(cvRound(vec.x * scale),
                                             cvRound(vec.y * scale));
    if (which == kMarkerMin)
        pt_min_ = onCircle;
    else if (which == kMarkerMax)
        pt_max_ = onCircle;
}
