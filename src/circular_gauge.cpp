#include "circular_gauge.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>

// ═══════════════════════════════════════════════════════════════════
//  Static: Find all gauges in frame
// ═══════════════════════════════════════════════════════════════════

std::vector<CircularGauge::ROI> CircularGauge::FindGauges(const cv::Mat& frame,
                                                int cannyThreshold,
                                                int /*accumulatorThreshold*/) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    int maxDim = std::max(gray.rows, gray.cols);
    double minArea = static_cast<double>(maxDim * maxDim) /
                     (kMinRadiusDivisor * kMinRadiusDivisor) * kPi;
    double maxArea = static_cast<double>(maxDim * maxDim) /
                     (kMaxRadiusDivisor * kMaxRadiusDivisor) * kPi;

    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred,
                     cv::Size(kGaussianBlurKernel, kGaussianBlurKernel),
                     kGaussianBlurSigma, kGaussianBlurSigma);

    cv::Mat edges;
    cv::Canny(blurred, edges, cannyThreshold, cannyThreshold / 2);

    // Dilate to close small gaps in contours
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::dilate(edges, edges, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges, contours, cv::RETR_LIST,
                     cv::CHAIN_APPROX_SIMPLE);

    // Fit ellipses to qualifying contours
    struct Candidate {
        cv::Point center;
        int radius;
        double area;
        cv::RotatedRect ellipse;
        cv::Mat H;
        cv::Size outSize;
    };
    std::vector<Candidate> candidates;

    for (const auto& cnt : contours) {
        if (cnt.size() < 5) continue;

        cv::RotatedRect rr = cv::fitEllipse(cnt);
        float a = rr.size.width * 0.5f;
        float b = rr.size.height * 0.5f;
        if (a < 1.0f || b < 1.0f) continue;

        // Reject very elongated ellipses (aspect ratio > 3:1)
        float major = std::max(a, b);
        float minor = std::min(a, b);
        if (major / minor > 3.0f) continue;

        double area = cv::contourArea(cnt);
        if (area < minArea || area > maxArea) continue;

        // Compute homography from the fitted ellipse
        cv::Mat H;
        cv::Size outSize;
        cv::Point center;
        if (!HomographyFromEllipse(rr, H, outSize, center)) continue;

        candidates.push_back({center, cvRound(major), area, rr, H, outSize});
    }

    // Sort by area descending, keep largest
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.area > b.area;
              });

    // Deduplicate: collapse candidates whose centers are close
    std::vector<CircularGauge::ROI> result;
    for (const auto& c : candidates) {
        bool duplicate = false;
        for (const auto& existing : result) {
            if (cv::norm(c.center - existing.center) <
                existing.radius * kDuplicateDistFactor) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            result.push_back({c.center, c.radius, c.ellipse, true,
                              c.H, c.outSize});
        }
        if (result.size() >= kMaxCirclesToKeep) break;
    }

    return result;
}

// ═══════════════════════════════════════════════════════════════════
//  Mask
// ═══════════════════════════════════════════════════════════════════

cv::Mat CircularGauge::CreateMask(const cv::Mat& frame) const {
    cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
    cv::Point center = detectionCenter();
    cv::circle(mask, center, roi_.radius, cv::Scalar(255), -1);
    return mask;
}

cv::Point CircularGauge::detectionCenter() const {
    if (hasHomography_)
        return cv::Point(cvRound(rectCenter_.x), cvRound(rectCenter_.y));
    return roi_.center;
}

// ═══════════════════════════════════════════════════════════════════
//  Coloured Needle Detection
// ═══════════════════════════════════════════════════════════════════

double CircularGauge::DetectColoredNeedle(const cv::Mat& frame) const {
    cv::Mat mask = CreateMask(frame);
    cv::Mat masked;
    frame.copyTo(masked, mask);

    cv::Point center = detectionCenter();

    cv::Mat hsv;
    cv::cvtColor(masked, hsv, cv::COLOR_BGR2HSV);

    cv::Mat red1, red2, redMask;
    cv::inRange(hsv, cv::Scalar(0, 50, 50), cv::Scalar(10, 255, 255), red1);
    cv::inRange(hsv, cv::Scalar(160, 50, 50), cv::Scalar(179, 255, 255), red2);
    cv::bitwise_or(red1, red2, redMask);

    cv::Mat kernel = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(kMorphKernelSize, kMorphKernelSize));
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
        if (area < roi_.radius * kMinNeedleAreaFactor) continue;
        cv::Moments m = cv::moments(contours[i]);
        if (m.m00 == 0) continue;
        cv::Point centroid(cvRound(m.m10 / m.m00), cvRound(m.m01 / m.m00));
        if (cv::norm(centroid - center) >
            roi_.radius * kMaxCentroidDistFactor) continue;
        double maxDist = 0;
        for (const auto& pt : contours[i])
            maxDist = std::max(maxDist, cv::norm(pt - center));
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
        double d = cv::norm(pt - center);
        if (d > maxDist) {
            maxDist = d;
            tip = pt;
        }
    }
    return std::atan2(tip.y - center.y, tip.x - center.x);
}

// ═══════════════════════════════════════════════════════════════════
//  Radial Needle Detection
// ═══════════════════════════════════════════════════════════════════

CircularGauge::RadialScanResult CircularGauge::ScanRadialLine(
    const cv::Mat& binary, const cv::Mat& mask, double angle) const {
    cv::Point center = detectionCenter();
    int startR = cvRound(roi_.radius * kRadialScanStartFactor);
    int endR = roi_.radius;
    int longestRun = 0, darkRun = 0, totalDark = 0, totalScan = 0;
    bool inRun = false;

    for (int r = startR; r < endR; r++) {
        int x = cvRound(center.x + r * std::cos(angle));
        int y = cvRound(center.y + r * std::sin(angle));
        if (x < 0 || x >= binary.cols || y < 0 || y >= binary.rows) break;
        if (mask.at<uchar>(y, x) == 0) break;
        totalScan++;
        if (binary.at<uchar>(y, x) > 0) {
            totalDark++;
            if (!inRun) {
                inRun = true;
                darkRun = 1;
            } else {
                darkRun++;
            }
            if (darkRun > longestRun) longestRun = darkRun;
        } else {
            inRun = false;
        }
    }
    if (inRun && darkRun > longestRun) longestRun = darkRun;

    double density = totalScan > 0 ? static_cast<double>(totalDark) / totalScan : 0;
    double reach = totalScan > 0 ? static_cast<double>(longestRun) / roi_.radius : 0;
    return {density, reach};
}

double CircularGauge::DetectNeedleRadial(const cv::Mat& frame) const {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::Mat mask = CreateMask(frame);
    cv::Mat masked;
    gray.copyTo(masked, mask);

    cv::Mat blurred;
    cv::GaussianBlur(masked, blurred,
                     cv::Size(kMorphKernelSize, kMorphKernelSize), 0);
    cv::Mat binary;
    cv::adaptiveThreshold(blurred, binary, 255,
                          cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV,
                          kAdaptiveThreshBlockSize, kAdaptiveThreshC);

    double bestScore = 0;
    double bestAngle = -1;

    for (int i = 0; i < kRadialScanAngles; i++) {
        double angle = 2.0 * kPi * i / kRadialScanAngles;
        auto [density, reach] = ScanRadialLine(binary, mask, angle);
        double score = density * kNeedleDensityWeight + reach * kNeedleReachWeight;
        if (score > bestScore) {
            bestScore = score;
            bestAngle = angle;
        }
    }
    return bestAngle;
}

std::optional<double> CircularGauge::DetectNeedle(const cv::Mat& frame) {
    if (hasHomography_) {
        cv::Mat warped = WarpFrame(frame);
        angle_ = DetectColoredNeedle(warped);
        if (angle_ < 0) {
            angle_ = DetectNeedleRadial(warped);
        }
    } else {
        angle_ = DetectColoredNeedle(frame);
        if (angle_ < 0) {
            angle_ = DetectNeedleRadial(frame);
        }
    }

    if (angle_ >= 0) {
        last_reading_ = AngleToValue(angle_);
        AddReading(last_reading_);
        return last_reading_;
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::FinalizeCalibration() {
    if (hasHomography_) {
        // Map markers to rectified space to compute angles in the circle
        std::vector<cv::Point2f> src = {
            cv::Point2f(static_cast<float>(pt_min_.x),
                        static_cast<float>(pt_min_.y)),
            cv::Point2f(static_cast<float>(pt_max_.x),
                        static_cast<float>(pt_max_.y))
        };
        std::vector<cv::Point2f> dst;
        cv::perspectiveTransform(src, dst, homography_);
        scale_.start_angle =
            std::atan2(dst[0].y - rectCenter_.y, dst[0].x - rectCenter_.x);
        scale_.end_angle =
            std::atan2(dst[1].y - rectCenter_.y, dst[1].x - rectCenter_.x);
    } else {
        scale_.start_angle =
            std::atan2(pt_min_.y - roi_.center.y, pt_min_.x - roi_.center.x);
        scale_.end_angle =
            std::atan2(pt_max_.y - roi_.center.y, pt_max_.x - roi_.center.x);
    }
    scale_.valid = true;
}

void CircularGauge::SetCalibrationValues(double minVal, double maxVal) {
    Gauge::SetCalibrationValues(minVal, maxVal);
    scale_.min_value = minVal;
    scale_.max_value = maxVal;
    scale_.valid = true;
}

// ═══════════════════════════════════════════════════════════════════
//  Angle-to-Value
// ═══════════════════════════════════════════════════════════════════

std::optional<double> CircularGauge::AngleToValue(double needleAngle) const {
    if (!scale_.valid || needleAngle < 0) return std::nullopt;
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
            return std::nullopt;
    }
    pos = std::clamp(pos, 0.0, 1.0);
    return scale_.min_value + pos * (scale_.max_value - scale_.min_value);
}

// ═══════════════════════════════════════════════════════════════════
//  Overlay Drawing
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::DrawOverlay(cv::Mat& frame, int labelY) {
    if (roi_.radius > 0) {
        if (hasHomography_) {
            // When homography is active, draw the warped circle outline
            // projected back into the original frame via inverse homography
            cv::Mat Hinv;
            cv::invert(homography_, Hinv);
            int side = std::min(warpSize_.width, warpSize_.height);
            int drawR = side / 2;
            // Draw ~20 points around the rectified circle, warped back
            std::vector<cv::Point2f> circlePts;
            for (int i = 0; i < 40; i++) {
                double a = 2.0 * kPi * i / 40.0;
                circlePts.emplace_back(
                    rectCenter_.x + drawR * static_cast<float>(std::cos(a)),
                    rectCenter_.y + drawR * static_cast<float>(std::sin(a)));
            }
            std::vector<cv::Point2f> srcPts;
            cv::perspectiveTransform(circlePts, srcPts, Hinv);
            for (size_t i = 0; i < srcPts.size(); i++) {
                size_t j = (i + 1) % srcPts.size();
                cv::line(frame,
                         cv::Point(cvRound(srcPts[i].x), cvRound(srcPts[i].y)),
                         cv::Point(cvRound(srcPts[j].x), cvRound(srcPts[j].y)),
                         color_, kCircleThickness, cv::LINE_AA);
            }
        } else {
            cv::circle(frame, roi_.center, roi_.radius, color_,
                       kCircleThickness);
        }

        if (scale_.valid) {
            if (hasHomography_) {
                // Map start/end points from rectified space back to original
                float arcR = roi_.radius * kRadiusInset;
                float sx = rectCenter_.x + arcR * static_cast<float>(std::cos(scale_.start_angle));
                float sy = rectCenter_.y + arcR * static_cast<float>(std::sin(scale_.start_angle));
                float ex = rectCenter_.x + arcR * static_cast<float>(std::cos(scale_.end_angle));
                float ey = rectCenter_.y + arcR * static_cast<float>(std::sin(scale_.end_angle));
                cv::Mat Hinv;
                cv::invert(homography_, Hinv);
                std::vector<cv::Point2f> src, dst;
                src.emplace_back(sx, sy);
                src.emplace_back(ex, ey);
                cv::perspectiveTransform(src, dst, Hinv);
                cv::Point startPt(cvRound(dst[0].x), cvRound(dst[0].y));
                cv::Point endPt(cvRound(dst[1].x), cvRound(dst[1].y));
                cv::line(frame, roi_.center, startPt, cv::Scalar(0, 255, 0), 2);
                cv::putText(frame,
                            std::to_string(static_cast<int>(scale_.min_value)),
                            startPt + cv::Point(10, 0),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 255, 0), 2);
                cv::line(frame, roi_.center, endPt, cv::Scalar(0, 0, 255), 2);
                cv::putText(frame,
                            std::to_string(static_cast<int>(scale_.max_value)),
                            endPt + cv::Point(10, 0),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 0, 255), 2);
            } else {
                int arcR = cvRound(roi_.radius * kRadiusInset);
                cv::Point startPt(
                    roi_.center.x + cvRound(arcR * std::cos(scale_.start_angle)),
                    roi_.center.y + cvRound(arcR * std::sin(scale_.start_angle)));
                cv::line(frame, roi_.center, startPt,
                         cv::Scalar(0, 255, 0), 2);
                cv::putText(frame,
                            std::to_string(static_cast<int>(scale_.min_value)),
                            startPt + cv::Point(10, 0),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 255, 0), 2);
                cv::Point endPt(
                    roi_.center.x + cvRound(arcR * std::cos(scale_.end_angle)),
                    roi_.center.y + cvRound(arcR * std::sin(scale_.end_angle)));
                cv::line(frame, roi_.center, endPt,
                         cv::Scalar(0, 0, 255), 2);
                cv::putText(frame,
                            std::to_string(static_cast<int>(scale_.max_value)),
                            endPt + cv::Point(10, 0),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6,
                            cv::Scalar(0, 0, 255), 2);
            }
        }
        if (angle_ >= 0) {
            if (hasHomography_) {
                // Map needle tip from rectified space back to original frame
                float tipX = rectCenter_.x +
                    roi_.radius * kNeedleLengthFactor * static_cast<float>(std::cos(angle_));
                float tipY = rectCenter_.y +
                    roi_.radius * kNeedleLengthFactor * static_cast<float>(std::sin(angle_));
                cv::Mat Hinv;
                cv::invert(homography_, Hinv);
                std::vector<cv::Point2f> src, dst;
                src.emplace_back(rectCenter_.x, rectCenter_.y);
                src.emplace_back(tipX, tipY);
                cv::perspectiveTransform(src, dst, Hinv);
                cv::line(frame,
                         cv::Point(cvRound(dst[0].x), cvRound(dst[0].y)),
                         cv::Point(cvRound(dst[1].x), cvRound(dst[1].y)),
                         cv::Scalar(255, 0, 0), kNeedleThickness, cv::LINE_AA);
                cv::circle(frame,
                           cv::Point(cvRound(dst[0].x), cvRound(dst[0].y)),
                           kCenterDotRadius, color_, -1);
            } else {
                cv::Point needleTip(
                    roi_.center.x + cvRound(roi_.radius * kNeedleLengthFactor *
                                            std::cos(angle_)),
                    roi_.center.y + cvRound(roi_.radius * kNeedleLengthFactor *
                                            std::sin(angle_)));
                cv::line(frame, roi_.center, needleTip,
                         cv::Scalar(255, 0, 0), kNeedleThickness);
                cv::circle(frame, roi_.center, kCenterDotRadius, color_, -1);
            }
        }
    }

    DrawGaugeNumber(frame);

    std::ostringstream oss;
    oss << "Value: ";
    if (smoothed_reading_.has_value())
        oss << std::fixed << std::setprecision(2) << *smoothed_reading_;
    else
        oss << "---";
    cv::putText(frame, oss.str(), cv::Point(30, labelY),
                cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 255, 255), 3);
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration Overlay Drawing
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::DrawCalibrationOverlay(cv::Mat& frame) {
    if (pt_min_ == cv::Point() && pt_max_ == cv::Point()) return;

    int thickness = 1;

    if (hasHomography_) {
        // Draw center dot in original frame
        cv::circle(frame, roi_.center, kCalibCenterDotRadius,
                   cv::Scalar(255, 255, 255), -1);

        // Map markers to rectified space to get their angles
        std::vector<cv::Point2f> src = {
            cv::Point2f(static_cast<float>(pt_min_.x),
                        static_cast<float>(pt_min_.y)),
            cv::Point2f(static_cast<float>(pt_max_.x),
                        static_cast<float>(pt_max_.y))
        };
        std::vector<cv::Point2f> dst;
        cv::perspectiveTransform(src, dst, homography_);

        // Compute angles in rectified space
        double a0 = std::atan2(dst[0].y - rectCenter_.y,
                               dst[0].x - rectCenter_.x);
        double a1 = std::atan2(dst[1].y - rectCenter_.y,
                               dst[1].x - rectCenter_.x);

        // Sample points along the arc in rectified space (circle),
        // then inverse-warp them to get the correct arc on the ellipse
        double arcR = roi_.radius * kRadiusInset;
        double span = a1 - a0;
        if (span < 0) span += 2.0 * kPi;

        constexpr int kArcSamples = 60;
        std::vector<cv::Point2f> arcSrc, arcDst;
        for (int i = 0; i <= kArcSamples; i++) {
            double t = a0 + span * i / kArcSamples;
            arcSrc.emplace_back(
                rectCenter_.x + arcR * static_cast<float>(std::cos(t)),
                rectCenter_.y + arcR * static_cast<float>(std::sin(t)));
        }
        cv::Mat Hinv;
        cv::invert(homography_, Hinv);
        cv::perspectiveTransform(arcSrc, arcDst, Hinv);

        for (size_t i = 0; i + 1 < arcDst.size(); i++) {
            cv::line(frame,
                     cv::Point(cvRound(arcDst[i].x), cvRound(arcDst[i].y)),
                     cv::Point(cvRound(arcDst[i + 1].x), cvRound(arcDst[i + 1].y)),
                     cv::Scalar(0, 255, 255), thickness, cv::LINE_AA);
        }

        // Draw min/max markers inset along the ellipse
        std::vector<cv::Point2f> pts;
        pts.emplace_back(static_cast<float>(pt_min_.x),
                         static_cast<float>(pt_min_.y));
        pts.emplace_back(static_cast<float>(pt_max_.x),
                         static_cast<float>(pt_max_.y));
        std::vector<cv::Point2f> ptsRect;
        cv::perspectiveTransform(pts, ptsRect, homography_);

        // Scale toward rectified center by kRadiusInset
        std::vector<cv::Point2f> ptsInset;
        for (auto& p : ptsRect) {
            float dx = p.x - rectCenter_.x;
            float dy = p.y - rectCenter_.y;
            ptsInset.emplace_back(rectCenter_.x + dx * kRadiusInset,
                                  rectCenter_.y + dy * kRadiusInset);
        }
        std::vector<cv::Point2f> ptsOriginal;
        cv::perspectiveTransform(ptsInset, ptsOriginal, Hinv);
        cv::Point ptMinIn(cvRound(ptsOriginal[0].x), cvRound(ptsOriginal[0].y));
        cv::Point ptMaxIn(cvRound(ptsOriginal[1].x), cvRound(ptsOriginal[1].y));

        // Draw min marker
        cv::circle(frame, ptMinIn, kCalibPtRadius, cv::Scalar(0, 255, 0), -1);
        cv::circle(frame, ptMinIn, kCalibPtOutlineRadius,
                   cv::Scalar(0, 255, 0), thickness);
        cv::putText(frame, "min", ptMinIn + cv::Point(12, 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        // Draw max marker
        cv::circle(frame, ptMaxIn, kCalibPtRadius, cv::Scalar(0, 0, 255), -1);
        cv::circle(frame, ptMaxIn, kCalibPtOutlineRadius,
                   cv::Scalar(0, 0, 255), thickness);
        cv::putText(frame, "max", ptMaxIn + cv::Point(12, 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);

    } else {
        // Original circular path
        int arcR = cvRound(roi_.radius * kRadiusInset);

        cv::circle(frame, roi_.center, kCalibCenterDotRadius,
                   cv::Scalar(255, 255, 255), -1);

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

        cv::circle(frame, ptMinIn, kCalibPtRadius, cv::Scalar(0, 255, 0), -1);
        cv::circle(frame, ptMinIn, kCalibPtOutlineRadius,
                   cv::Scalar(0, 255, 0), thickness);
        cv::putText(frame, "min", ptMinIn + cv::Point(12, 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 255, 0), 1);

        cv::circle(frame, ptMaxIn, kCalibPtRadius, cv::Scalar(0, 0, 255), -1);
        cv::circle(frame, ptMaxIn, kCalibPtOutlineRadius,
                   cv::Scalar(0, 0, 255), thickness);
        cv::putText(frame, "max", ptMaxIn + cv::Point(12, 4),
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 255), 1);
    }
}

// ═══════════════════════════════════════════════════════════════════
//  Static helpers
// ═══════════════════════════════════════════════════════════════════

int CircularGauge::HandleClick(int clickX, int clickY) {
    int thresh = std::max(roi_.radius / 6, static_cast<int>(kHitTestMinThresh));
    cv::Point click(clickX, clickY);

    cv::Point ptMinTarget, ptMaxTarget;
    if (hasHomography_) {
        // Compute inset positions along the ellipse via rectified space
        std::vector<cv::Point2f> pts;
        pts.emplace_back(static_cast<float>(pt_min_.x),
                         static_cast<float>(pt_min_.y));
        pts.emplace_back(static_cast<float>(pt_max_.x),
                         static_cast<float>(pt_max_.y));
        std::vector<cv::Point2f> ptsRect;
        cv::perspectiveTransform(pts, ptsRect, homography_);
        std::vector<cv::Point2f> ptsInset;
        for (auto& p : ptsRect) {
            float dx = p.x - rectCenter_.x;
            float dy = p.y - rectCenter_.y;
            ptsInset.emplace_back(rectCenter_.x + dx * kRadiusInset,
                                  rectCenter_.y + dy * kRadiusInset);
        }
        cv::Mat Hinv;
        cv::invert(homography_, Hinv);
        std::vector<cv::Point2f> ptsOriginal;
        cv::perspectiveTransform(ptsInset, ptsOriginal, Hinv);
        ptMinTarget = cv::Point(cvRound(ptsOriginal[0].x),
                                cvRound(ptsOriginal[0].y));
        ptMaxTarget = cv::Point(cvRound(ptsOriginal[1].x),
                                cvRound(ptsOriginal[1].y));
    } else {
        cv::Point center = roi_.center;
        cv::Point vecMin = pt_min_ - center;
        cv::Point vecMax = pt_max_ - center;
        ptMinTarget = center + cv::Point(
            cvRound(vecMin.x * kRadiusInset), cvRound(vecMin.y * kRadiusInset));
        ptMaxTarget = center + cv::Point(
            cvRound(vecMax.x * kRadiusInset), cvRound(vecMax.y * kRadiusInset));
    }

    int dMin = cvRound(cv::norm(click - ptMinTarget));
    int dMax = cvRound(cv::norm(click - ptMaxTarget));
    int hit = kMarkerNone;
    if (dMin <= thresh && dMin <= dMax)
        hit = kMarkerMin;
    else if (dMax <= thresh)
        hit = kMarkerMax;
    if (hit != kMarkerNone) {
        MoveMarker(hit, click);
    }
    return hit;
}

// ═══════════════════════════════════════════════════════════════════
//  Static helpers
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::DrawOutline(cv::Mat& img) const {
    if (roi_.radius <= 0) return;
    if (hasHomography_) {
        cv::ellipse(img, ellipseRect_, color_, kCircleThickness, cv::LINE_AA);
    } else {
        cv::circle(img, roi_.center, roi_.radius, color_, kCircleThickness);
    }
    DrawGaugeNumber(img);
}

CircularGauge::CircularGauge(const cv::Point& center, int radius,
                             const cv::Scalar& color)
    : Gauge(center, radius, color)
{
    // Default markers at 135° (top-right) and 45° (top-left)
    double a = 3.0 * kPi / 4.0;
    pt_min_ = center + cv::Point(cvRound(radius * std::cos(a)),
                                 cvRound(radius * std::sin(a)));
    a = kPi / 4.0;
    pt_max_ = center + cv::Point(cvRound(radius * std::cos(a)),
                                 cvRound(radius * std::sin(a)));
}

void CircularGauge::MoveMarker(int which, cv::Point click) {
    cv::Point onPerimeter;

    if (hasHomography_) {
        // Map click to rectified space, project onto circle, map back
        std::vector<cv::Point2f> src = {
            cv::Point2f(static_cast<float>(click.x),
                        static_cast<float>(click.y))
        };
        std::vector<cv::Point2f> rectPt;
        cv::perspectiveTransform(src, rectPt, homography_);

        float dx = rectPt[0].x - rectCenter_.x;
        float dy = rectPt[0].y - rectCenter_.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f) return;

        float R = static_cast<float>(roi_.radius);
        float scale = R / dist;
        cv::Point2f markerRect(rectCenter_.x + dx * scale,
                               rectCenter_.y + dy * scale);

        cv::Mat Hinv;
        cv::invert(homography_, Hinv);
        std::vector<cv::Point2f> src2 = {markerRect};
        std::vector<cv::Point2f> dst2;
        cv::perspectiveTransform(src2, dst2, Hinv);
        onPerimeter = cv::Point(cvRound(dst2[0].x), cvRound(dst2[0].y));
    } else {
        cv::Point vec = click - roi_.center;
        double dist = cv::norm(vec);
        if (dist < 1.0) return;
        double scale = static_cast<double>(roi_.radius) / dist;
        onPerimeter = roi_.center + cv::Point(cvRound(vec.x * scale),
                                              cvRound(vec.y * scale));
    }

    if (which == kMarkerMin)
        pt_min_ = onPerimeter;
    else if (which == kMarkerMax)
        pt_max_ = onPerimeter;
}

// ═══════════════════════════════════════════════════════════════════
//  Ellipse-to-Circle Homography
// ═══════════════════════════════════════════════════════════════════

bool CircularGauge::ComputeHomography(const std::vector<cv::Point>& pts,
                                      cv::Mat& H, cv::Size& outSize,
                                      cv::RotatedRect& ellipseRect,
                                      cv::Point& inferredCenter) {
    if (pts.size() < 5) return false;

    // Use OpenCV's robust ellipse fitter (handles noisy points)
    std::vector<cv::Point2f> ptsF;
    ptsF.reserve(pts.size());
    for (const auto& p : pts)
        ptsF.emplace_back(static_cast<float>(p.x), static_cast<float>(p.y));

    cv::RotatedRect rr = cv::fitEllipse(ptsF);

    float a = rr.size.width * 0.5f;   // semi-major or semi-minor
    float b = rr.size.height * 0.5f;
    if (a < 1.0f || b < 1.0f) return false;

    float cx = rr.center.x;
    float cy = rr.center.y;
    inferredCenter = cv::Point(cvRound(cx), cvRound(cy));

    // Ensure a >= b for the eigendecomposition below
    bool swapped = false;
    if (a < b) { std::swap(a, b); swapped = true; }

    // Angle from RotatedRect (degrees → radians)
    double theta = rr.angle * kPi / 180.0;
    if (swapped) theta += kPi / 2.0;  // adjust if axes were swapped

    // Conic matrix M from semi-axes and rotation:
    //   cos²θ/a² + sin²θ/b²       (cosθ sinθ)(1/a² - 1/b²)
    //   (cosθ sinθ)(1/a² - 1/b²)  sin²θ/a² + cos²θ/b²
    double cosT = std::cos(theta), sinT = std::sin(theta);
    double invA2 = 1.0 / (a * a);
    double invB2 = 1.0 / (b * b);

    double M00 = cosT * cosT * invA2 + sinT * sinT * invB2;
    double M01 = cosT * sinT * (invA2 - invB2);
    double M11 = sinT * sinT * invA2 + cosT * cosT * invB2;

    // Eigendecomposition of M
    double trace = M00 + M11;
    double detM = M00 * M11 - M01 * M01;
    double disc = std::sqrt(std::max(0.0, trace * trace * 0.25 - detM));
    double lamBig = trace * 0.5 + disc;
    double lamSmall = trace * 0.5 - disc;
    if (lamSmall < 1e-15) return false;

    // Eigenvector for lamSmall (= semi-MAJOR axis direction)
    double ex, ey;
    if (std::abs(M01) > 1e-15) {
        ex = lamSmall - M11;
        ey = M01;
    } else if (std::abs(M00 - lamSmall) > std::abs(M11 - lamSmall)) {
        ex = M01;
        ey = lamSmall - M00;
    } else {
        ex = 1.0;
        ey = 0.0;
    }
    double len = std::sqrt(ex * ex + ey * ey);
    if (len < 1e-15) return false;
    ex /= len;
    ey /= len;

    double s1 = std::sqrt(lamBig);
    double s2 = std::sqrt(lamSmall);

    double h00 = s1 * ey * ey + s2 * ex * ex;
    double h01 = (s2 - s1) * ex * ey;
    double h10 = h01;
    double h11 = s1 * ex * ex + s2 * ey * ey;

    // Output radius: average distance from center to points
    double R = 0;
    for (const auto& p : pts) {
        double dx = p.x - cx, dy = p.y - cy;
        R += std::sqrt(dx * dx + dy * dy);
    }
    R /= static_cast<double>(pts.size());

    // Full homography with translation to (R, R)
    H = cv::Mat::eye(3, 3, CV_64F);
    H.at<double>(0, 0) = R * h00;
    H.at<double>(0, 1) = R * h01;
    H.at<double>(0, 2) = R * (1.0 - h00 * cx - h01 * cy);
    H.at<double>(1, 0) = R * h10;
    H.at<double>(1, 1) = R * h11;
    H.at<double>(1, 2) = R * (1.0 - h10 * cx - h11 * cy);

    int side = cvRound(R * 2.0);
    outSize = cv::Size(side, side);
    ellipseRect = rr;

    return true;
}

bool CircularGauge::HomographyFromEllipse(const cv::RotatedRect& rr,
                                          cv::Mat& H, cv::Size& outSize,
                                          cv::Point& inferredCenter) {
    float a = rr.size.width * 0.5f;
    float b = rr.size.height * 0.5f;
    if (a < 1.0f || b < 1.0f) return false;

    float cx = rr.center.x;
    float cy = rr.center.y;
    inferredCenter = cv::Point(cvRound(cx), cvRound(cy));

    bool swapped = false;
    if (a < b) { std::swap(a, b); swapped = true; }

    double theta = rr.angle * kPi / 180.0;
    if (swapped) theta += kPi / 2.0;

    double cosT = std::cos(theta), sinT = std::sin(theta);
    double invA2 = 1.0 / (a * a);
    double invB2 = 1.0 / (b * b);

    double M00 = cosT * cosT * invA2 + sinT * sinT * invB2;
    double M01 = cosT * sinT * (invA2 - invB2);
    double M11 = sinT * sinT * invA2 + cosT * cosT * invB2;

    double trace = M00 + M11;
    double detM = M00 * M11 - M01 * M01;
    double disc = std::sqrt(std::max(0.0, trace * trace * 0.25 - detM));
    double lamBig = trace * 0.5 + disc;
    double lamSmall = trace * 0.5 - disc;
    if (lamSmall < 1e-15) return false;

    double ex, ey;
    if (std::abs(M01) > 1e-15) {
        ex = lamSmall - M11;
        ey = M01;
    } else if (std::abs(M00 - lamSmall) > std::abs(M11 - lamSmall)) {
        ex = M01;
        ey = lamSmall - M00;
    } else {
        ex = 1.0;
        ey = 0.0;
    }
    double len = std::sqrt(ex * ex + ey * ey);
    if (len < 1e-15) return false;
    ex /= len;
    ey /= len;

    double s1 = std::sqrt(lamBig);
    double s2 = std::sqrt(lamSmall);

    double h00 = s1 * ey * ey + s2 * ex * ex;
    double h01 = (s2 - s1) * ex * ey;
    double h10 = h01;
    double h11 = s1 * ex * ex + s2 * ey * ey;

    // R = average of semi-axes (circle radius in rectified space)
    double R = (a + b) * 0.5;

    H = cv::Mat::eye(3, 3, CV_64F);
    H.at<double>(0, 0) = R * h00;
    H.at<double>(0, 1) = R * h01;
    H.at<double>(0, 2) = R * (1.0 - h00 * cx - h01 * cy);
    H.at<double>(1, 0) = R * h10;
    H.at<double>(1, 1) = R * h11;
    H.at<double>(1, 2) = R * (1.0 - h10 * cx - h11 * cy);

    int side = cvRound(R * 2.0);
    outSize = cv::Size(side, side);

    return true;
}

void CircularGauge::SetHomography(const cv::Mat& H, const cv::Size& outSize,
                                   cv::Point center,
                                   cv::RotatedRect ellipseRect) {
    homography_ = H.clone();
    homographyBase_ = H.clone();
    warpSize_ = outSize;
    rectCenter_ = cv::Point2f(outSize.width * 0.5f, outSize.height * 0.5f);
    ellipseRect_ = ellipseRect;
    hasHomography_ = true;

    roi_.center = center;
    roi_.radius = cvRound(std::min(outSize.width, outSize.height) * 0.5);

    // Remap initial pt_min_/pt_max_ from the circle onto the ellipse:
    // map them to rectified space (they'll land on the circle), then
    // map them back — since MoveMarker constrains to the ellipse, we
    // just do the same projection MoveMarker does.
    auto remapToEllipse = [&](cv::Point& pt) {
        std::vector<cv::Point2f> src = {
            cv::Point2f(static_cast<float>(pt.x), static_cast<float>(pt.y))
        };
        std::vector<cv::Point2f> rectPt;
        cv::perspectiveTransform(src, rectPt, homography_);
        float dx = rectPt[0].x - rectCenter_.x;
        float dy = rectPt[0].y - rectCenter_.y;
        float dist = std::sqrt(dx * dx + dy * dy);
        if (dist < 1.0f) return;
        float R = static_cast<float>(roi_.radius);
        float scale = R / dist;
        cv::Point2f markerRect(rectCenter_.x + dx * scale,
                               rectCenter_.y + dy * scale);
        cv::Mat Hinv;
        cv::invert(homography_, Hinv);
        std::vector<cv::Point2f> src2 = {markerRect};
        std::vector<cv::Point2f> dst2;
        cv::perspectiveTransform(src2, dst2, Hinv);
        pt = cv::Point(cvRound(dst2[0].x), cvRound(dst2[0].y));
    };

    remapToEllipse(pt_min_);
    remapToEllipse(pt_max_);
}

cv::Mat CircularGauge::WarpFrame(const cv::Mat& frame) const {
    if (!hasHomography_ || homography_.empty()) return frame;
    cv::Mat warped;
    cv::warpPerspective(frame, warped, homography_, warpSize_,
                        cv::INTER_LINEAR, cv::BORDER_REFLECT_101);
    return warped;
}

// ═══════════════════════════════════════════════════════════════════
//  Motion Compensation
// ═══════════════════════════════════════════════════════════════════

void CircularGauge::ResetMotionState() {
    Gauge::ResetMotionState();
    if (hasHomography_ && !homographyBase_.empty())
        homography_ = homographyBase_.clone();
}

void CircularGauge::InitMotionFeatures(const cv::Mat& frame) {
    roi_ref_ = roi_;

    cv::cvtColor(frame, ref_gray_, cv::COLOR_BGR2GRAY);

    // Create circular mask for feature detection inside the gauge face
    cv::Mat mask = cv::Mat::zeros(ref_gray_.size(), CV_8UC1);
    // Use a slightly smaller circle to avoid edge artifacts
    int maskRadius = cvRound(roi_.radius * 0.85);
    cv::circle(mask, roi_.center, maskRadius, cv::Scalar(255), -1);

    cv::goodFeaturesToTrack(ref_gray_, ref_features_, kMaxFeatures,
                            kFeatureQuality, kFeatureBlockSize, mask,
                            kMinFeatureDist);

    prev_gray_ = ref_gray_.clone();
    prev_features_ = ref_features_;
}

void CircularGauge::UpdateROI(const cv::Mat& frame) {
    if (ref_features_.empty()) return;

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    if (prev_gray_.empty()) {
        prev_gray_ = gray.clone();
        return;
    }

    // Track features from previous frame to current frame
    std::vector<cv::Point2f> currentPts;
    std::vector<uchar> status;
    std::vector<float> err;

    cv::calcOpticalFlowPyrLK(prev_gray_, gray, prev_features_, currentPts,
                              status, err);

    // Filter: keep only successfully tracked points
    std::vector<cv::Point2f> trackedPrev, trackedCurr;
    for (size_t i = 0; i < status.size(); i++) {
        if (status[i]) {
            trackedPrev.push_back(prev_features_[i]);
            trackedCurr.push_back(currentPts[i]);
        }
    }

    // Need enough points to estimate a reliable transform
    if (trackedCurr.size() < kMinPointsForTransform) {
        prev_gray_ = gray.clone();
        return;
    }

    // Compute affine transform (rotation + uniform scale + translation)
    // from reference features to current features.
    // First, map reference features to current frame's tracked positions.
    // We need: ref -> current mapping, so we need to find the transform
    // that maps ref_features_ positions to the tracked positions.
    //
    // Since we track prev->current, and prev was the last known position,
    // we accumulate: compose the new delta with the existing transform.

    // Compute rigid transform from reference to current tracked points.
    // We have trackedPrev (positions in prev frame) and trackedCurr (positions in current frame).
    // We also know the mapping from ref to trackedPrev (accumulated from previous steps).
    // For simplicity, we re-derive the transform from reference to current each frame
    // by chaining: ref -> prev -> current.

    // Compute transform from ref to prev (using ref_features_ mapped to trackedPrev)
    // Actually, we need to track from ref directly. Let's re-approach:
    // We track prev->current each frame. To get ref->current, we need the
    // accumulated displacement. A cleaner approach: track from ref directly.

    // Alternative simpler approach: use optical flow from reference to current.
    // Re-detect or re-track from reference each time is expensive.
    // Instead, accumulate the translation/transform.

    // Best approach: compute transform from ref_features_ to current positions
    // by propagating through the chain. We keep prev_features_ as the positions
    // of the reference features in the previous frame. So prev_features_ IS
    // the current known positions of ref_features_. We track prev->current,
    // so currentPts gives us the new positions of ref_features_.

    // Compute affine transform from ref_features_ (reference positions)
    // to the newly tracked positions (currentPts filtered by status).
    if (trackedCurr.size() >= kMinPointsForTransform) {
        // We need ref features that correspond to tracked points.
        // Since we filter by status, trackedPrev[i] = prev_features_[j]
        // where j is the index that succeeded. But prev_features_ contains
        // ALL reference feature positions (accumulated). So trackedPrev
        // already contains the reference-mapped positions from the previous step.

        // Actually, let me restructure: prev_features_ always holds the
        // positions of ref_features_ in the previous frame. After tracking,
        // currentPts holds positions in current frame. So:
        // ref_features_[i] -> prev_features_[i] (known from last frame)
        // prev_features_[i] -> currentPts[i] (just computed)
        // We want: ref_features_[i] -> currentPts[i]

        // For a rigid/affine transform, we can directly compute ref->current.
        // We need the subset of ref_features_ that were successfully tracked.
        std::vector<cv::Point2f> refSub, currSub;
        for (size_t i = 0; i < status.size(); i++) {
            if (status[i]) {
                refSub.push_back(ref_features_[i]);
                currSub.push_back(currentPts[i]);
            }
        }

        if (refSub.size() >= kMinPointsForTransform) {
            cv::Mat inliers;
            cv::Mat H = cv::estimateAffinePartial2D(refSub, currSub, inliers,
                                                    cv::RANSAC,
                                                    kInlierReprojThresh);
            if (!H.empty()) {
                // Apply transform to reference center
                cv::Point2f refCenter(static_cast<float>(roi_ref_.center.x),
                                      static_cast<float>(roi_ref_.center.y));
                std::vector<cv::Point2f> transformed;
                cv::transform(std::vector<cv::Point2f>{refCenter}, transformed, H);
                cv::Point2f newCenter = transformed[0];

                roi_.center = cv::Point(cvRound(newCenter.x),
                                        cvRound(newCenter.y));

                // Estimate scale from the transform matrix
                double scaleX = std::sqrt(H.at<double>(0, 0) * H.at<double>(0, 0) +
                                          H.at<double>(0, 1) * H.at<double>(0, 1));
                roi_.radius = cvRound(roi_ref_.radius * scaleX);

                // Shift the homography to follow the moved ellipse
                if (hasHomography_ && !homographyBase_.empty()) {
                    double dx = newCenter.x - roi_ref_.center.x;
                    double dy = newCenter.y - roi_ref_.center.y;
                    cv::Mat T = cv::Mat::eye(3, 3, CV_64F);
                    T.at<double>(0, 2) = -dx;
                    T.at<double>(1, 2) = -dy;
                    homography_ = homographyBase_ * T;
                }
            }
        }
    }

    // Update tracking state for next frame
    prev_gray_ = gray.clone();
    prev_features_ = currentPts;
}
