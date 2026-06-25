#include "edgewise_gauge.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>

// ═══════════════════════════════════════════════════════════════════
//  Construction
// ═══════════════════════════════════════════════════════════════════

EdgewiseGauge::EdgewiseGauge(const cv::Rect& bezelRect, const cv::Scalar& color)
    : bezelRect_(bezelRect)
    , bezelRectRef_(bezelRect)
    , orientation_(bezelRect.width >= bezelRect.height
                       ? InstrumentOrientation::kHorizontal
                       : InstrumentOrientation::kVertical)
{
    roi_.center = cv::Point(bezelRect.x + bezelRect.width / 2,
                            bezelRect.y + bezelRect.height / 2);
    roi_.radius = 0;
    color_ = color;

    // Default calibration markers: full extent of the bezel center axis
    if (orientation_ == InstrumentOrientation::kHorizontal) {
        int cy = bezelRect.y + bezelRect.height / 2;
        pt_min_ = cv::Point(bezelRect.x, cy);
        pt_max_ = cv::Point(bezelRect.x + bezelRect.width, cy);
    } else {
        int cx = bezelRect.x + bezelRect.width / 2;
        pt_min_ = cv::Point(cx, bezelRect.y);
        pt_max_ = cv::Point(cx, bezelRect.y + bezelRect.height);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Static: Find all edgewise gauges in frame
// ═══════════════════════════════════════════════════════════════════

std::vector<cv::Rect> EdgewiseGauge::FindGauges(const cv::Mat& frame,
                                                  int cannyThreshold) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred,
                     cv::Size(kEdgewiseBlurKernel, kEdgewiseBlurKernel), 0);

    cv::Mat edges;
    cv::Canny(blurred, edges, cannyThreshold, cannyThreshold / 2);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::dilate(edges, edges, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);

    struct Candidate {
        cv::Rect rect;
        double area;
    };
    std::vector<Candidate> candidates;

    for (const auto& c : contours) {
        std::vector<cv::Point> approx;
        cv::approxPolyDP(c, approx, 0.02 * cv::arcLength(c, true), true);

        if (approx.size() != 4 || !cv::isContourConvex(approx))
            continue;

        double area = cv::contourArea(approx);
        if (area < kEdgewiseMinArea)
            continue;

        cv::Rect bbox = cv::boundingRect(approx);
        double aspect = static_cast<double>(bbox.width) / bbox.height;
        if (aspect < kEdgewiseMinAspect || aspect > kEdgewiseMaxAspect)
            continue;

        candidates.push_back({bbox, area});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.area > b.area;
              });

    std::vector<cv::Rect> result;
    for (const auto& c : candidates) {
        cv::Point center(c.rect.x + c.rect.width / 2,
                         c.rect.y + c.rect.height / 2);
        bool duplicate = false;
        for (const auto& existing : result) {
            cv::Point ec(existing.x + existing.width / 2,
                         existing.y + existing.height / 2);
            double dist = cv::norm(center - ec);
            double maxDist = (std::min)(existing.width, existing.height) *
                             kEdgewiseDuplicateDistFactor;
            if (dist < maxDist) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
            result.push_back(c.rect);
        if (result.size() >= kEdgewiseMaxToKeep)
            break;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════
//  Scale Strip Detection (Stage 2)
// ═══════════════════════════════════════════════════════════════════

cv::Rect EdgewiseGauge::detectScaleStrip(const cv::Mat& roiColor) const {
    cv::Mat hsv;
    cv::cvtColor(roiColor, hsv, cv::COLOR_BGR2HSV);

    // Isolate the white/light scale background
    cv::Mat mask;
    cv::inRange(hsv, cv::Scalar(0, 0, kEdgewiseScaleLowV),
                cv::Scalar(180, kEdgewiseScaleMaxSat, 255), mask);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT,
                                                cv::Size(5, 5));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, kernel);

    std::vector<cv::Point> pts;
    cv::findNonZero(mask, pts);
    if (pts.empty())
        return cv::Rect(0, 0, roiColor.cols, roiColor.rows);

    return cv::boundingRect(pts);
}

// ═══════════════════════════════════════════════════════════════════
//  Needle Detection — Red (color segmentation)
// ═══════════════════════════════════════════════════════════════════

std::optional<double> EdgewiseGauge::detectRedNeedle(
    const cv::Mat& roiHsv) const {
    cv::Mat mask1, mask2, needleMask;
    cv::inRange(roiHsv, cv::Scalar(0, 100, 100), cv::Scalar(10, 255, 255),
                mask1);
    cv::inRange(roiHsv, cv::Scalar(160, 100, 100), cv::Scalar(180, 255, 255),
                mask2);
    needleMask = mask1 | mask2;

    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_RECT, cv::Size(kEdgewiseMorphKernel, kEdgewiseMorphKernel));
    cv::morphologyEx(needleMask, needleMask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(needleMask, needleMask, cv::MORPH_OPEN, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(needleMask, contours, cv::RETR_EXTERNAL,
                     cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty())
        return std::nullopt;

    // Find the largest red contour
    int bestIdx = -1;
    double bestArea = 0;
    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area > bestArea) {
            bestArea = area;
            bestIdx = static_cast<int>(i);
        }
    }
    if (bestIdx < 0)
        return std::nullopt;

    cv::Moments m = cv::moments(contours[bestIdx]);
    if (m.m00 == 0)
        return std::nullopt;

    cv::Point centroid(cvRound(m.m10 / m.m00), cvRound(m.m01 / m.m00));

    if (orientation_ == InstrumentOrientation::kHorizontal)
        return static_cast<double>(centroid.x);
    else
        return static_cast<double>(centroid.y);
}

// ═══════════════════════════════════════════════════════════════════
//  Needle Detection — Dark (Hough lines)
// ═══════════════════════════════════════════════════════════════════

std::optional<double> EdgewiseGauge::detectDarkNeedle(
    const cv::Mat& roiGray) const {
    cv::Mat blurred;
    cv::GaussianBlur(roiGray, blurred, cv::Size(3, 3), 0);

    cv::Mat edges;
    cv::Canny(blurred, edges, 50, 150);

    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, CV_PI / 180, 30, 20, 5);

    double bestPos = -1;
    int bestLen = 0;

    for (const auto& l : lines) {
        int x1 = l[0], y1 = l[1], x2 = l[2], y2 = l[3];
        double angle = std::atan2(y2 - y1, x2 - x1) * 180.0 / CV_PI;
        int length = cvRound(cv::norm(cv::Point(x2 - x1, y2 - y1)));

        if (orientation_ == InstrumentOrientation::kHorizontal) {
            // Needle should be near-vertical (angle close to ±90°)
            if (std::abs(angle) < 70)
                continue;
        } else {
            // Needle should be near-horizontal (angle close to 0° or 180°)
            if (std::abs(angle) > 20 && std::abs(angle) < 160)
                continue;
        }

        // Check minimum length relative to scale strip
        int stripSpan = (orientation_ == InstrumentOrientation::kHorizontal)
                            ? scaleStrip_.height
                            : scaleStrip_.width;
        if (length < stripSpan * kEdgewiseMinNeedleLength)
            continue;

        if (length > bestLen) {
            bestLen = length;
            if (orientation_ == InstrumentOrientation::kHorizontal)
                bestPos = (x1 + x2) / 2.0;
            else
                bestPos = (y1 + y2) / 2.0;
        }
    }

    if (bestLen == 0)
        return std::nullopt;
    return bestPos;
}

// ═══════════════════════════════════════════════════════════════════
//  Needle Detection — Orchestrator
// ═══════════════════════════════════════════════════════════════════

std::optional<double> EdgewiseGauge::detectNeedlePosition(
    const cv::Mat& roiColor) const {
    cv::Mat hsv;
    cv::cvtColor(roiColor, hsv, cv::COLOR_BGR2HSV);

    auto pos = detectRedNeedle(hsv);
    if (pos.has_value())
        return pos;

    cv::Mat gray;
    cv::cvtColor(roiColor, gray, cv::COLOR_BGR2GRAY);
    return detectDarkNeedle(gray);
}

// ═══════════════════════════════════════════════════════════════════
//  Value Mapping (Stage 4)
// ═══════════════════════════════════════════════════════════════════

double EdgewiseGauge::positionToValue(double needlePos) const {
    double scaleStart, scaleEnd;
    if (orientation_ == InstrumentOrientation::kHorizontal) {
        scaleStart = scaleStrip_.x;
        scaleEnd = scaleStrip_.x + scaleStrip_.width;
    } else {
        scaleStart = scaleStrip_.y;
        scaleEnd = scaleStrip_.y + scaleStrip_.height;
    }

    double span = scaleEnd - scaleStart;
    if (std::abs(span) < 1e-6)
        return min_value_;

    double normalized = (needlePos - scaleStart) / span;
    normalized = std::clamp(normalized, 0.0, 1.0);
    return min_value_ + normalized * (max_value_ - min_value_);
}

// ═══════════════════════════════════════════════════════════════════
//  Gauge Interface — DetectNeedle (Stage 3 + 4)
// ═══════════════════════════════════════════════════════════════════

std::optional<double> EdgewiseGauge::DetectNeedle(const cv::Mat& frame) {
    if (bezelRect_.x < 0 || bezelRect_.y < 0 ||
        bezelRect_.x + bezelRect_.width > frame.cols ||
        bezelRect_.y + bezelRect_.height > frame.rows) {
        last_reading_ = std::nullopt;
        AddReading(std::nullopt);
        return std::nullopt;
    }

    cv::Mat roi = frame(bezelRect_);

    auto needlePos = detectNeedlePosition(roi);
    if (!needlePos.has_value()) {
        last_reading_ = std::nullopt;
        AddReading(std::nullopt);
        return std::nullopt;
    }

    last_reading_ = positionToValue(*needlePos);
    AddReading(last_reading_);
    return last_reading_;
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration
// ═══════════════════════════════════════════════════════════════════

void EdgewiseGauge::FinalizeCalibration() {
    // Scale strip is detected lazily in InitMotionFeatures or DetectNeedle
    // when the frame is available. Default to full bezel for now.
    scaleStrip_ = cv::Rect(0, 0, bezelRect_.width, bezelRect_.height);
    calib_values_set_ = true;
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Markers — Click / Drag
// ═══════════════════════════════════════════════════════════════════

CalibrationMarker EdgewiseGauge::HandleClick(int clickX, int clickY) {
    cv::Point click(clickX, clickY);
    int dMin = cvRound(cv::norm(click - pt_min_));
    int dMax = cvRound(cv::norm(click - pt_max_));

    if (dMin <= kEdgewiseHitThresh && dMin <= dMax)
        return CalibrationMarker::kMin;
    if (dMax <= kEdgewiseHitThresh)
        return CalibrationMarker::kMax;
    return CalibrationMarker::kNone;
}

void EdgewiseGauge::MoveMarker(CalibrationMarker which, cv::Point click) {
    auto& pt = (which == CalibrationMarker::kMin) ? pt_min_ : pt_max_;

    if (orientation_ == InstrumentOrientation::kHorizontal) {
        pt.x = std::clamp(click.x,
                           bezelRect_.x,
                           bezelRect_.x + bezelRect_.width);
        pt.y = bezelRect_.y + bezelRect_.height / 2;
    } else {
        pt.y = std::clamp(click.y,
                           bezelRect_.y,
                           bezelRect_.y + bezelRect_.height);
        pt.x = bezelRect_.x + bezelRect_.width / 2;
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Motion Compensation
// ═══════════════════════════════════════════════════════════════════

void EdgewiseGauge::InitMotionFeatures(const cv::Mat& frame) {
    bezelRectRef_ = bezelRect_;
    roi_ref_.center = roi_.center;

    // Detect scale strip from the first frame
    if (bezelRect_.x >= 0 && bezelRect_.y >= 0 &&
        bezelRect_.x + bezelRect_.width <= frame.cols &&
        bezelRect_.y + bezelRect_.height <= frame.rows) {
        cv::Mat roiColor = frame(bezelRect_);
        scaleStrip_ = detectScaleStrip(roiColor);

        // Update calibration markers to detected scale strip edges
        if (orientation_ == InstrumentOrientation::kHorizontal) {
            int cy = bezelRect_.y + scaleStrip_.y + scaleStrip_.height / 2;
            pt_min_ = cv::Point(bezelRect_.x + scaleStrip_.x, cy);
            pt_max_ = cv::Point(bezelRect_.x + scaleStrip_.x + scaleStrip_.width,
                                cy);
        } else {
            int cx = bezelRect_.x + scaleStrip_.x + scaleStrip_.width / 2;
            pt_min_ = cv::Point(cx, bezelRect_.y + scaleStrip_.y);
            pt_max_ = cv::Point(cx, bezelRect_.y + scaleStrip_.y + scaleStrip_.height);
        }
    }

    cv::cvtColor(frame, ref_gray_, cv::COLOR_BGR2GRAY);

    cv::Mat mask = cv::Mat::zeros(ref_gray_.size(), CV_8UC1);
    cv::rectangle(mask, bezelRect_, cv::Scalar(255), -1);

    cv::goodFeaturesToTrack(ref_gray_, ref_features_, kMaxFeatures,
                            kFeatureQuality, kFeatureBlockSize, mask,
                            kMinFeatureDist);

    prev_gray_ = ref_gray_.clone();
    prev_features_ = ref_features_;
}

void EdgewiseGauge::UpdateROI(const cv::Mat& frame) {
    if (ref_features_.empty()) return;

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    if (prev_gray_.empty()) {
        prev_gray_ = gray.clone();
        return;
    }

    std::vector<cv::Point2f> currentPts;
    std::vector<uchar> status;
    std::vector<float> err;

    cv::calcOpticalFlowPyrLK(prev_gray_, gray, prev_features_, currentPts,
                              status, err);

    std::vector<cv::Point2f> refSub, currSub;
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            refSub.push_back(ref_features_[i]);
            currSub.push_back(currentPts[i]);
        }
    }

    if (refSub.size() < kMinPointsForTransform) {
        prev_gray_ = gray.clone();
        return;
    }

    cv::Mat inliers;
    cv::Mat H = cv::estimateAffinePartial2D(refSub, currSub, inliers,
                                            cv::RANSAC,
                                            kInlierReprojThresh);
    if (!H.empty()) {
        cv::Point2f refCenter(
            static_cast<float>(bezelRectRef_.x + bezelRectRef_.width / 2),
            static_cast<float>(bezelRectRef_.y + bezelRectRef_.height / 2));
        std::vector<cv::Point2f> transformed;
        cv::transform(std::vector<cv::Point2f>{refCenter}, transformed, H);
        cv::Point2f newCenter = transformed[0];

        double dx = newCenter.x - refCenter.x;
        double dy = newCenter.y - refCenter.y;

        bezelRect_.x = cvRound(bezelRectRef_.x + dx);
        bezelRect_.y = cvRound(bezelRectRef_.y + dy);

        roi_.center = cv::Point(cvRound(newCenter.x),
                                cvRound(newCenter.y));
    }

    prev_gray_ = gray.clone();
    prev_features_ = currentPts;
}

void EdgewiseGauge::ResetMotionState() {
    bezelRect_ = bezelRectRef_;
    roi_.center = roi_ref_.center;
}

// ═══════════════════════════════════════════════════════════════════
//  Drawing — Outline (Detection mode)
// ═══════════════════════════════════════════════════════════════════

void EdgewiseGauge::DrawOutline(cv::Mat& img) const {
    cv::rectangle(img, bezelRect_, color_, 2, cv::LINE_AA);    
}

// ═══════════════════════════════════════════════════════════════════
//  Drawing — Calibration Overlay
// ═══════════════════════════════════════════════════════════════════

void EdgewiseGauge::DrawCalibrationOverlay(cv::Mat& frame) {
    if (pt_min_ == cv::Point() && pt_max_ == cv::Point()) return;

    cv::Scalar green(0, 255, 0);
    cv::Scalar red(0, 0, 255);

    auto drawMarker = [&](cv::Point pt, const cv::Scalar& color, const char* label) {
        cv::circle(frame, pt, kEdgewiseMarkerRadius, color, -1);
        cv::circle(frame, pt, kEdgewiseMarkerOutlineRadius, color, 1);
        cv::putText(frame, label, pt + cv::Point(12, 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 1);
    };

    if (orientation_ == InstrumentOrientation::kHorizontal) {
        int y0 = bezelRect_.y;
        int y1 = bezelRect_.y + bezelRect_.height;
        cv::line(frame, cv::Point(pt_min_.x, y0), cv::Point(pt_min_.x, y1),
                 green, 1, cv::LINE_AA);
        cv::line(frame, cv::Point(pt_max_.x, y0), cv::Point(pt_max_.x, y1),
                 red, 1, cv::LINE_AA);
    } else {
        int x0 = bezelRect_.x;
        int x1 = bezelRect_.x + bezelRect_.width;
        cv::line(frame, cv::Point(x0, pt_min_.y), cv::Point(x1, pt_min_.y),
                 green, 1, cv::LINE_AA);
        cv::line(frame, cv::Point(x0, pt_max_.y), cv::Point(x1, pt_max_.y),
                 red, 1, cv::LINE_AA);
    }

    drawMarker(pt_min_, green, "min");
    drawMarker(pt_max_, red, "max");
}

// ═══════════════════════════════════════════════════════════════════
//  Drawing — Processing Overlay
// ═══════════════════════════════════════════════════════════════════

void EdgewiseGauge::DrawOverlay(cv::Mat& frame, int labelY) const {
    // Bezel outline
    cv::rectangle(frame, bezelRect_, color_, 2, cv::LINE_AA);

    // Scale strip outline
    cv::Rect absStrip(bezelRect_.x + scaleStrip_.x,
                      bezelRect_.y + scaleStrip_.y,
                      scaleStrip_.width, scaleStrip_.height);
    cv::rectangle(frame, absStrip, cv::Scalar(200, 200, 200), 1, cv::LINE_AA);

    // Draw needle line
    if (last_reading_.has_value()) {
        // Avoid division by zero if min and max values are equal
        if (std::abs(max_value_ - min_value_) < 1e-6)
            return;

        if (orientation_ == InstrumentOrientation::kHorizontal) {
            // Map value back to X for drawing
            double normalized = (*last_reading_ - min_value_) /
                                (max_value_ - min_value_);
            int needleX = cvRound(absStrip.x + normalized * absStrip.width);
            cv::line(frame,
                     cv::Point(needleX, absStrip.y),
                     cv::Point(needleX, absStrip.y + absStrip.height),
                     cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
        } else {
            double normalized = (*last_reading_ - min_value_) /
                                (max_value_ - min_value_);
            int needleY = cvRound(absStrip.y + normalized * absStrip.height);
            cv::line(frame,
                     cv::Point(absStrip.x, needleY),
                     cv::Point(absStrip.x + absStrip.width, needleY),
                     cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
        }
    }

    DrawGaugeNumber(frame);

    DrawValueText(frame, labelY);
}

// ═══════════════════════════════════════════════════════════════════
//  Manual Placement Instructions
// ═══════════════════════════════════════════════════════════════════

const char* EdgewiseGauge::manualInstruction(int stage) {
    switch (stage) {
    case 0: return "Click 4 CORNERS of the rectangular bezel";
    case 1: return "Corner 1/4 placed \u2014 click corner 2";
    case 2: return "Corner 2/4 placed \u2014 click corner 3";
    case 3: return "Corner 3/4 placed \u2014 click corner 4";
    default: return "";
    }
}

std::optional<cv::Rect> EdgewiseGauge::FitFromManualEdges(
    const std::vector<cv::Point>& edges) {
    if (edges.size() < kManualClicks)
        return std::nullopt;

    auto boundingRect = cv::boundingRect(edges);
    qDebug() << "Manual edgewise gauge at (" << boundingRect.x << ","
             << boundingRect.y << ")," << boundingRect.width << "x"
             << boundingRect.height;
    return boundingRect;
}
