#include "gauge_detector.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// ═══════════════════════════════════════════════════════════════════
//  Circle Detection
// ═══════════════════════════════════════════════════════════════════

bool GaugeDetector::detectCircle(const cv::Mat &frame) {
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

  int maxDim = std::max(gray.rows, gray.cols);
  int minR = maxDim / 15;
  int maxR = maxDim / 2;

  struct Params {
    double dp;
    int canny;
    int acc;
  };
  std::vector<Params> paramSets = {
      {1, 80, 40},    {1, 100, 50},  {1, 60, 30},  {1.2, 80, 40},
      {1.2, 100, 50}, {1.5, 80, 30}, {2, 100, 40},
  };

  for (const auto &p : paramSets) {
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);
    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(blurred, circles, cv::HOUGH_GRADIENT, p.dp, gray.rows / 6,
                     p.canny, p.acc, minR, maxR);
    if (!circles.empty()) {
      const auto &c = circles[0];
      m_gauge = {cv::Point(cvRound(c[0]), cvRound(c[1])), cvRound(c[2])};
      return true;
    }
  }

  for (const auto &p : paramSets) {
    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, p.dp, gray.rows / 6,
                     p.canny, p.acc, minR, maxR);
    if (!circles.empty()) {
      const auto &c = circles[0];
      m_gauge = {cv::Point(cvRound(c[0]), cvRound(c[1])), cvRound(c[2])};
      return true;
    }
  }

  return false;
}

void GaugeDetector::setCircle(const cv::Point &center, int radius) {
  m_gauge = {center, radius};
}

// ═══════════════════════════════════════════════════════════════════
//  Static: Find all gauges in frame
// ═══════════════════════════════════════════════════════════════════

std::vector<GaugeROI> GaugeDetector::findGauges(const cv::Mat &frame,
                                                int cannyThreshold,
                                                int accumulatorThreshold) {
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

  int maxDim = std::max(gray.rows, gray.cols);
  int minR = maxDim / 15;
  int maxR = maxDim / 2;

  double dpValues[] = {1.0, 1.5};
  std::vector<cv::Vec3f> allCircles;

  for (double dp : dpValues) {
    cv::Mat blurred;
    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);    
    cv::HoughCircles(blurred, allCircles, cv::HOUGH_GRADIENT, 1.0, gray.rows / 6,
                     cannyThreshold, accumulatorThreshold, minR, maxR);
                  
    int max_circles_to_keep = 5; // Set your limit here

    if (allCircles.size() > max_circles_to_keep) {
        allCircles.resize(max_circles_to_keep); // Keep only the best matches
    }                     
  }

  std::vector<GaugeROI> result;
  for (const auto &c : allCircles) {
    cv::Point center(cvRound(c[0]), cvRound(c[1]));
    int radius = cvRound(c[2]);
    bool duplicate = false;
    for (const auto &existing : result) {
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

cv::Mat GaugeDetector::createMask(const cv::Mat &frame) const {
  cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
  cv::circle(mask, m_gauge.center, m_gauge.radius, cv::Scalar(255), -1);
  return mask;
}

// ═══════════════════════════════════════════════════════════════════
//  Coloured Needle Detection
// ═══════════════════════════════════════════════════════════════════

double GaugeDetector::detectColoredNeedle(const cv::Mat &frame) const {
  cv::Mat mask = createMask(frame);
  cv::Mat masked;
  frame.copyTo(masked, mask);

  cv::Mat hsv;
  cv::cvtColor(masked, hsv, cv::COLOR_BGR2HSV);

  cv::Mat red1, red2, redMask;
  cv::inRange(hsv, cv::Scalar(0, 50, 50), cv::Scalar(10, 255, 255), red1);
  cv::inRange(hsv, cv::Scalar(160, 50, 50), cv::Scalar(179, 255, 255), red2);
  cv::bitwise_or(red1, red2, redMask);

  cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
  cv::morphologyEx(redMask, redMask, cv::MORPH_CLOSE, kernel);
  cv::morphologyEx(redMask, redMask, cv::MORPH_OPEN, kernel);

  std::vector<std::vector<cv::Point>> contours;
  cv::findContours(redMask, contours, cv::RETR_EXTERNAL,
                   cv::CHAIN_APPROX_SIMPLE);
  if (contours.empty())
    return -1;

  int bestIdx = -1;
  double bestScore = 0;
  for (size_t i = 0; i < contours.size(); i++) {
    double area = cv::contourArea(contours[i]);
    if (area < m_gauge.radius * 0.5)
      continue;
    cv::Moments m = cv::moments(contours[i]);
    if (m.m00 == 0)
      continue;
    cv::Point centroid(cvRound(m.m10 / m.m00), cvRound(m.m01 / m.m00));
    if (cv::norm(centroid - m_gauge.center) > m_gauge.radius * 0.6)
      continue;
    double maxDist = 0;
    for (const auto &pt : contours[i])
      maxDist = std::max(maxDist, cv::norm(pt - m_gauge.center));
    double score = maxDist * std::log(area + 1);
    if (score > bestScore) {
      bestScore = score;
      bestIdx = i;
    }
  }

  if (bestIdx < 0)
    return -1;

  double maxDist = 0;
  cv::Point tip;
  for (const auto &pt : contours[bestIdx]) {
    double d = cv::norm(pt - m_gauge.center);
    if (d > maxDist) {
      maxDist = d;
      tip = pt;
    }
  }
  return std::atan2(tip.y - m_gauge.center.y, tip.x - m_gauge.center.x);
}

// ═══════════════════════════════════════════════════════════════════
//  Radial Needle Detection
// ═══════════════════════════════════════════════════════════════════

double GaugeDetector::detectNeedleRadial(const cv::Mat &frame) const {
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
  cv::Mat mask = createMask(frame);
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
  int startR = cvRound(m_gauge.radius * 0.08);
  int endR = m_gauge.radius;

  for (int i = 0; i < numAngles; i++) {
    double angle = 2.0 * PI * i / numAngles;
    int longestRun = 0, darkRun = 0, totalDark = 0, totalScan = 0;
    bool inRun = false;
    for (int r = startR; r < endR; r++) {
      int x = cvRound(m_gauge.center.x + r * std::cos(angle));
      int y = cvRound(m_gauge.center.y + r * std::sin(angle));
      if (x < 0 || x >= binary.cols || y < 0 || y >= binary.rows)
        break;
      if (mask.at<uchar>(y, x) == 0)
        break;
      totalScan++;
      if (binary.at<uchar>(y, x) > 0) {
        totalDark++;
        if (!inRun) {
          inRun = true;
          darkRun = 1;
        } else
          darkRun++;
        if (darkRun > longestRun)
          longestRun = darkRun;
      } else
        inRun = false;
    }
    if (inRun && darkRun > longestRun)
      longestRun = darkRun;
    double density = totalScan > 0 ? (double)totalDark / totalScan : 0;
    double reach = totalScan > 0 ? (double)longestRun / m_gauge.radius : 0;
    double score = density * 0.4 + reach * 0.6;
    if (score > bestScore) {
      bestScore = score;
      bestAngle = angle;
    }
  }
  return bestAngle;
}

double GaugeDetector::smoothReadings(double value) {
  if (value >= 0) {
    valueHistory.push_back(value);
    if (valueHistory.size() > smoothWindow)
      valueHistory.pop_front();
  }

  smoothedValue = value;
  if (!valueHistory.empty()) {
    smoothedValue =
        std::accumulate(valueHistory.begin(), valueHistory.end(), 0.0) /
        valueHistory.size();
  }
  return smoothedValue;
}

double GaugeDetector::detectNeedle(const cv::Mat &frame) {
  angle = detectColoredNeedle(frame);
  if (angle < 0) {
    angle = detectNeedleRadial(frame);
  }

  if (angle >= 0) {
    value = angleToValue(angle);    
    smoothReadings(value);
    return value;
  }
  return -1;
}

// ═══════════════════════════════════════════════════════════════════
//  Calibration
// ═══════════════════════════════════════════════════════════════════

void GaugeDetector::calibrateFromPoints(const cv::Point &ptMin,
                                        const cv::Point &ptMax) {
  m_scale.startAngle =
      std::atan2(ptMin.y - m_gauge.center.y, ptMin.x - m_gauge.center.x);
  m_scale.endAngle =
      std::atan2(ptMax.y - m_gauge.center.y, ptMax.x - m_gauge.center.x);
}

void GaugeDetector::setCalibrationValues(double minVal, double maxVal) {
  m_scale.minValue = minVal;
  m_scale.maxValue = maxVal;
}

void GaugeDetector::setCalibrationValid(bool valid) { m_scale.valid = valid; }

void GaugeDetector::setStartAngle(double a) { m_scale.startAngle = a; }

void GaugeDetector::setEndAngle(double a) { m_scale.endAngle = a; }

// ═══════════════════════════════════════════════════════════════════
//  Angle-to-Value
// ═══════════════════════════════════════════════════════════════════

double GaugeDetector::angleToValue(double needleAngle) const {
  if (!m_scale.valid || needleAngle < 0)
    return -1;
  auto normalize = [](double a) {
    a = std::fmod(a, 2.0 * PI);
    return a < 0 ? a + 2.0 * PI : a;
  };
  double start = normalize(m_scale.startAngle);
  double end = normalize(m_scale.endAngle);
  double needle = normalize(needleAngle);

  double range = (end > start) ? (end - start) : ((2.0 * PI - start) + end);
  double pos;
  if (end > start) {
    if (needle >= start && needle <= end)
      pos = (needle - start) / range;
    else if (needle < start)
      pos = ((needle + 2.0 * PI) - start) / range;
    else
      pos = (needle - start) / range;
  } else {
    if (needle >= start)
      pos = (needle - start) / range;
    else if (needle <= end)
      pos = ((needle + 2.0 * PI) - start) / range;
    else
      return -1;
  }
  pos = std::clamp(pos, 0.0, 1.0);
  return m_scale.minValue + pos * (m_scale.maxValue - m_scale.minValue);
}

// ═══════════════════════════════════════════════════════════════════
//  Overlay Drawing
// ═══════════════════════════════════════════════════════════════════

void GaugeDetector::drawOverlay(cv::Mat &frame, int labelY) const {
  if (m_gauge.radius > 0) {
    cv::circle(frame, m_gauge.center, m_gauge.radius, m_color, 2);
    if (m_scale.valid) {
      cv::Point startPt(
          m_gauge.center.x +
              cvRound(m_gauge.radius * 0.85 * std::cos(m_scale.startAngle)),
          m_gauge.center.y +
              cvRound(m_gauge.radius * 0.85 * std::sin(m_scale.startAngle)));
      cv::line(frame, m_gauge.center, startPt, cv::Scalar(0, 255, 0), 2);
      cv::putText(frame, std::to_string((int)m_scale.minValue),
                  startPt + cv::Point(10, 0), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                  cv::Scalar(0, 255, 0), 2);
      cv::Point endPt(m_gauge.center.x + cvRound(m_gauge.radius * 0.85 *
                                                 std::cos(m_scale.endAngle)),
                      m_gauge.center.y + cvRound(m_gauge.radius * 0.85 *
                                                 std::sin(m_scale.endAngle)));
      cv::line(frame, m_gauge.center, endPt, cv::Scalar(0, 0, 255), 2);
      cv::putText(frame, std::to_string((int)m_scale.maxValue),
                  endPt + cv::Point(10, 0), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                  cv::Scalar(0, 0, 255), 2);
    }
    if (angle >= 0) {
      cv::Point needleTip(m_gauge.center.x + cvRound(m_gauge.radius * 0.8 *
                                                     std::cos(angle)),
                          m_gauge.center.y + cvRound(m_gauge.radius * 0.8 *
                                                     std::sin(angle)));
      cv::line(frame, m_gauge.center, needleTip, cv::Scalar(255, 0, 0), 3);
      cv::circle(frame, m_gauge.center, 5, m_color, -1);
    }
  }
  std::ostringstream oss;
  oss << "Value: " << std::fixed << std::setprecision(2) << value;
  cv::putText(frame, oss.str(), cv::Point(30, labelY), cv::FONT_HERSHEY_SIMPLEX,
              1.2, cv::Scalar(0, 255, 255), 3);
}

double GaugeDetector::getSmoothedValue() const { return smoothedValue; }

cv::Scalar GaugeDetector::nextColor() {
  static const std::vector<cv::Scalar> palette = {
      {0, 0, 255},     // red
      {255, 0, 0},     // blue
      {0, 255, 255},   // yellow
      {255, 0, 255},   // magenta
      {255, 255, 0},   // cyan
      {0, 165, 255},   // orange
      {128, 0, 128},   // purple
      {203, 192, 255}, // pink
      {0, 255, 0},     // green
  };
  static size_t idx = 0;
  return palette[idx++ % palette.size()];
}

void GaugeDetector::reset() {
  m_gauge = {};
  m_scale = {0, 0, 0, 1000, false};
  m_state = GaugeState::INIT;
  m_circleStage = 0;
  m_circleCenter = {};
  m_circleRadius = 0;
  m_ptMin = {};
  m_ptMax = {};
  m_calibTrackMin = 0;
  m_calibTrackMax = 1000;
  valueHistory.clear();
  smoothedValue = 0;
  angle = -1;
  value = -1;
}

// ═══════════════════════════════════════════════════════════════════
//  Ring Scan (static)
// ═══════════════════════════════════════════════════════════════════

std::vector<TickMark> GaugeDetector::scanRingAtRadius(const cv::Mat &frame,
                                                      const GaugeROI &gauge,
                                                      double scanRadius,
                                                      int numAngles) {
  cv::Mat gray;
  cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
  std::vector<double> intensity(numAngles, 0);

  for (int i = 0; i < numAngles; i++) {
    double angle = 2.0 * PI * i / numAngles;
    double sum = 0;
    int count = 0;
    for (int dr = -2; dr <= 2; dr++) {
      double r = scanRadius + dr;
      int x = cvRound(gauge.center.x + r * std::cos(angle));
      int y = cvRound(gauge.center.y + r * std::sin(angle));
      if (x >= 0 && x < gray.cols && y >= 0 && y < gray.rows) {
        sum += gray.at<uchar>(y, x);
        count++;
      }
    }
    intensity[i] = count > 0 ? sum / count : 255;
  }

  int window = 5;
  std::vector<double> smoothed(numAngles, 0);
  for (int i = 0; i < numAngles; i++) {
    double s = 0;
    for (int w = -window; w <= window; w++)
      s += intensity[(i + w + numAngles) % numAngles];
    smoothed[i] = s / (2.0 * window + 1);
  }

  double globalMin = *std::min_element(smoothed.begin(), smoothed.end());
  double globalMax = *std::max_element(smoothed.begin(), smoothed.end());
  double threshold = globalMin + (globalMax - globalMin) * 0.4;

  std::vector<TickMark> marks;
  int start = -1;
  for (int i = 0; i <= numAngles; i++) {
    bool isMark = (i < numAngles) ? (smoothed[i] < threshold) : false;
    if (isMark && start == -1)
      start = i;
    else if (!isMark && start != -1) {
      int end = i - 1;
      if (end - start >= 3) {
        double centerAngle = 2.0 * PI * (start + end) / (2.0 * numAngles);
        double width = 2.0 * PI * (end - start + 1) / numAngles;
        double prominence = 0;
        for (int j = start; j <= end; j++)
          prominence += (255.0 - smoothed[j]);
        prominence /= (end - start + 1);
        marks.push_back({centerAngle, width, prominence, scanRadius});
      }
      start = -1;
    }
  }
  return marks;
}

// ═══════════════════════════════════════════════════════════════════
//  Even-spacing Refinement (static)
// ═══════════════════════════════════════════════════════════════════

std::vector<TickMark>
GaugeDetector::refineEvenSpacing(const std::vector<TickMark> &marks) {
  if (marks.size() < 3)
    return marks;
  std::vector<double> angles;
  for (const auto &m : marks)
    angles.push_back(m.angle);
  std::sort(angles.begin(), angles.end());

  std::vector<double> gaps;
  for (size_t i = 1; i < angles.size(); i++)
    gaps.push_back(angles[i] - angles[i - 1]);
  gaps.push_back((2.0 * PI - angles.back()) + angles.front());
  double minGap = 1.0 * PI / 180.0;
  gaps.erase(std::remove_if(gaps.begin(), gaps.end(),
                             [&](double g) { return g < minGap; }),
             gaps.end());
  if (gaps.empty())
    return marks;

  std::sort(gaps.begin(), gaps.end());
  double baseSpacing = gaps[gaps.size() / 2];
  int totalMarks = cvRound(2.0 * PI / baseSpacing);
  if (totalMarks < 3)
    return marks;
  baseSpacing = 2.0 * PI / totalMarks;

  auto angularDist = [](double a, double b) {
    double d = std::abs(a - b);
    return std::min(d, 2.0 * PI - d);
  };

  int bestCount = 0;
  double bestOffset = angles[0];
  for (auto off : angles) {
    int count = 0;
    for (auto a : angles) {
      double residue = std::fmod(angularDist(a, off), baseSpacing);
      if (residue < baseSpacing * 0.25 || residue > baseSpacing * 0.75)
        count++;
    }
    if (count > bestCount) {
      bestCount = count;
      bestOffset = off;
    }
  }

  double avgProminence = 0;
  for (const auto &m : marks)
    avgProminence += m.prominence;
  avgProminence /= marks.size();

  std::vector<TickMark> refined;
  for (int i = 0; i < totalMarks; i++) {
    double pos = std::fmod(bestOffset + i * baseSpacing, 2.0 * PI);
    if (pos < 0)
      pos += 2.0 * PI;
    bool matched = false;
    for (const auto &m : marks) {
      if (angularDist(m.angle, pos) < baseSpacing * 0.3) {
        refined.push_back(m);
        matched = true;
        break;
      }
    }
    if (!matched)
      refined.push_back({pos, 0, avgProminence, marks.front().distance});
  }
  std::sort(
      refined.begin(), refined.end(),
      [](const TickMark &a, const TickMark &b) { return a.angle < b.angle; });
  return refined;
}

void GaugeDetector::handleClick(int clickX, int clickY) {
  if (state() == GaugeState::CIRCLE_MANUAL) {
    if (circleStage() == 1) {
      setCircleCenter(cv::Point(clickX, clickY));
      std::cout << "  >> Center at (" << clickX << ", " << clickY << ")\n";
      setCircleStage(2);
    } else if (circleStage() == 2) {
      int r = cvRound(cv::norm(cv::Point(clickX, clickY) - circleCenter()));
      setCircleRadius(r);
      std::cout << "  >> Radius set to " << r << "\n";
      setCircleStage(3);
    }
  } else if (state() == GaugeState::CALIB_MIN) {
    setPtMin(cv::Point(clickX, clickY));
    std::cout << "  >> Min marking at (" << clickX << ", " << clickY << ")\n";
    setState(GaugeState::CALIB_MAX);
  } else if (state() == GaugeState::CALIB_MAX) {
    setPtMax(cv::Point(clickX, clickY));
    std::cout << "  >> Max marking at (" << clickX << ", " << clickY << ")\n";
    setState(GaugeState::CALIB_CONFIRM);
  }
}
