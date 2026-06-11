#include <opencv2/opencv.hpp>
#include <iostream>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <deque>
#include <fstream>

struct GaugeROI {
    cv::Point center;
    int radius;
};

struct ScaleCalibration {
    double startAngle;    // radians, angle for number "1"
    double endAngle;      // radians, angle for number "4"
    double minValue;
    double maxValue;
    bool valid;
};

struct CalibrationPoints {
    cv::Point pt0;   // position of number "0"
    cv::Point pt4;   // position of number "4"
    bool valid;
};

struct TickMark {
    double angle;       // radians
    double angularWidth; // radians
    double prominence;  // how prominent the mark is
    double distance;     // distance from center
};

static const double PI = 3.14159265358979323846;

// Mouse callback for manual calibration
static CalibrationPoints gCalibPoints;
static int gCalibStage = 0;  // 0=not started, 1=waiting for 1, 2=waiting for 4, 3=done

static void onMouseClick(int event, int x, int y, int flags, void *userdata) {
    if (event == cv::EVENT_LBUTTONDOWN && gCalibStage < 3) {
        if (gCalibStage == 1) {
            gCalibPoints.pt0 = cv::Point(x, y);
            std::cout << "  >> Number '0' marked at (" << x << ", " << y << ")\n";
            std::cout << "  >> Now click on number '4'...\n";
            gCalibStage = 2;
        } else if (gCalibStage == 2) {
            gCalibPoints.pt4 = cv::Point(x, y);
            std::cout << "  >> Number '4' marked at (" << x << ", " << y << ")\n";
            gCalibPoints.valid = true;
            gCalibStage = 3;
        }
    }
}

static cv::Point gCircleCenter;
static int gCircleRadius = 0;
static int gCircleStage = 0; // 0=not started, 1=click center, 2=click edge, 3=done

static void onCircleClick(int event, int x, int y, int flags, void *userdata) {
    if (event == cv::EVENT_LBUTTONDOWN) {
        if (gCircleStage == 1) {
            gCircleCenter = cv::Point(x, y);
            std::cout << "  >> Center set at (" << x << ", " << y << "). Now click on the edge.\n";
            gCircleStage = 2;
        } else if (gCircleStage == 2) {
            gCircleRadius = cvRound(cv::norm(cv::Point(x, y) - gCircleCenter));
            std::cout << "  >> Radius set to " << gCircleRadius << ".\n";
            gCircleStage = 3;
        }
    }
}

// Detect the gauge circle using multiple HoughCircles parameter sets,
// with a manual click fallback.
std::vector<GaugeROI> detectGaugeCircles(const cv::Mat &frame) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    int maxDim = std::max(gray.rows, gray.cols);
    int minR = maxDim / 15;
    int maxR = maxDim / 2;

    // Try multiple parameter combinations
    struct Params { double dp; int canny; int acc; };
    std::vector<Params> paramSets = {
        {1,   80,  40},
        {1,   100, 50},
        {1,   60,  30},
        {1.2, 80,  40},
        {1.2, 100, 50},
        {1.5, 80,  30},
        {2,   100, 40},
    };

    for (const auto &p : paramSets) {
        cv::Mat blurred;
        cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);

        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(blurred, circles, cv::HOUGH_GRADIENT, p.dp,
                         gray.rows / 6, p.canny, p.acc, minR, maxR);
        if (!circles.empty()) {
            std::vector<GaugeROI> gauges;
            for (const auto &c : circles) {
                gauges.push_back({cv::Point(cvRound(c[0]), cvRound(c[1])), cvRound(c[2])});
            }
            return gauges;
        }
    }

    // Try without blur
    for (const auto &p : paramSets) {
        std::vector<cv::Vec3f> circles;
        cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, p.dp,
                         gray.rows / 6, p.canny, p.acc, minR, maxR);
        if (!circles.empty()) {
            std::vector<GaugeROI> gauges;
            for (const auto &c : circles) {
                gauges.push_back({cv::Point(cvRound(c[0]), cvRound(c[1])), cvRound(c[2])});
            }
            return gauges;
        }
    }

    // All auto-detection failed — manual fallback
    std::cout << "  >> Auto circle detection failed.\n";
    std::cout << "  >> Click on the center of the gauge, then click on the edge.\n";

    gCircleStage = 1;
    cv::namedWindow("Manual Circle");
    cv::setMouseCallback("Manual Circle", onCircleClick, nullptr);

    while (gCircleStage < 3) {
        cv::Mat disp = frame.clone();
        if (gCircleStage == 1) {
            cv::putText(disp, "Click on the CENTER of the gauge",
                        cv::Point(30, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                        cv::Scalar(0, 255, 255), 2);
        } else if (gCircleStage == 2) {
            cv::circle(disp, gCircleCenter, 5, cv::Scalar(0, 255, 0), -1);
            cv::circle(disp, gCircleCenter, 30, cv::Scalar(0, 255, 0), 1);
            cv::putText(disp, "Now click on the EDGE of the gauge face",
                        cv::Point(30, 30), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                        cv::Scalar(0, 255, 255), 2);
        }
        cv::imshow("Manual Circle", disp);
        if (cv::waitKey(30) == 27) break;
    }

    cv::destroyWindow("Manual Circle");

    if (gCircleStage == 3 && gCircleRadius > 0) {
        std::vector<GaugeROI> gauges;
        gauges.push_back({gCircleCenter, gCircleRadius});
        return gauges;
    }

    return {};
}

// Create a circular mask for the gauge
cv::Mat createGaugeMask(const cv::Mat &frame, const GaugeROI &gauge) {
    cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
    cv::circle(mask, gauge.center, gauge.radius, cv::Scalar(255), -1);
    return mask;
}

// Try to detect a colored needle (e.g., red) using HSV color segmentation
double detectColoredNeedle(const cv::Mat &frame, const GaugeROI &gauge) {
    cv::Mat mask = createGaugeMask(frame, gauge);
    cv::Mat masked;
    frame.copyTo(masked, mask);

    cv::Mat hsv;
    cv::cvtColor(masked, hsv, cv::COLOR_BGR2HSV);

    // Red: two ranges in HSV (wraps around hue 0)
    cv::Mat red1, red2, redMask;
    cv::inRange(hsv, cv::Scalar(0, 50, 50), cv::Scalar(10, 255, 255), red1);
    cv::inRange(hsv, cv::Scalar(160, 50, 50), cv::Scalar(179, 255, 255), red2);
    cv::bitwise_or(red1, red2, redMask);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(5, 5));
    cv::morphologyEx(redMask, redMask, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(redMask, redMask, cv::MORPH_OPEN, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(redMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (contours.empty()) return -1;

    int bestIdx = -1;
    double bestScore = 0;

    for (size_t i = 0; i < contours.size(); i++) {
        double area = cv::contourArea(contours[i]);
        if (area < gauge.radius * 0.5) continue;

        cv::Moments m = cv::moments(contours[i]);
        if (m.m00 == 0) continue;
        cv::Point centroid(cvRound(m.m10 / m.m00), cvRound(m.m01 / m.m00));
        double distFromCenter = cv::norm(centroid - gauge.center);

        if (distFromCenter > gauge.radius * 0.6) continue;

        // Find the point farthest from center (needle tip)
        double maxDist = 0;
        for (const auto &pt : contours[i]) {
            double d = cv::norm(pt - gauge.center);
            if (d > maxDist) maxDist = d;
        }

        // Score: large area + extends far from center
        double score = maxDist * std::log(area + 1);
        if (score > bestScore) {
            bestScore = score;
            bestIdx = i;
        }
    }

    if (bestIdx < 0) return -1;

    // Compute angle from center to the farthest point of the needle contour
    double maxDist = 0;
    cv::Point tip;
    for (const auto &pt : contours[bestIdx]) {
        double d = cv::norm(pt - gauge.center);
        if (d > maxDist) {
            maxDist = d;
            tip = pt;
        }
    }

    return std::atan2(tip.y - gauge.center.y, tip.x - gauge.center.x);
}

// Detect the needle angle using a radial scan approach.
// For each angle, scan from near-center outward and measure how much dark
// pixels (needle body) are found. The needle produces a long solid dark run,
// while tick marks and numbers only produce short dark segments at the periphery.
double detectNeedleRadial(const cv::Mat &frame, const GaugeROI &gauge) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    cv::Mat mask = createGaugeMask(frame, gauge);
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

    int startR = cvRound(gauge.radius * 0.08);
    int endR = gauge.radius;

    for (int i = 0; i < numAngles; i++) {
        double angle = 2.0 * PI * i / numAngles;
        int darkRun = 0;
        int longestRun = 0;
        int totalDark = 0;
        int totalScan = 0;
        bool inRun = false;

        for (int r = startR; r < endR; r++) {
            int x = cvRound(gauge.center.x + r * std::cos(angle));
            int y = cvRound(gauge.center.y + r * std::sin(angle));

            if (x < 0 || x >= binary.cols || y < 0 || y >= binary.rows) break;
            if (mask.at<uchar>(y, x) == 0) break;

            totalScan++;
            bool isDark = (binary.at<uchar>(y, x) > 0);

            if (isDark) {
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

        // Score combines: long continuous dark run + high dark pixel density
        double density = (totalScan > 0) ? (static_cast<double>(totalDark) / totalScan) : 0;
        double reach = (totalScan > 0) ? (static_cast<double>(longestRun) / gauge.radius) : 0;
        double score = density * 0.4 + reach * 0.6;

        if (score > bestScore) {
            bestScore = score;
            bestAngle = angle;
        }
    }

    return bestAngle;
}

// Detect the needle angle by trying multiple methods
double detectNeedleAngle(const cv::Mat &frame, const GaugeROI &gauge) {
    double angle = detectColoredNeedle(frame, gauge);
    if (angle >= 0) return angle;
    return detectNeedleRadial(frame, gauge);
}

// Scan a thin ring at a user-specified radius to find all marking angles
std::vector<TickMark> scanRingAtRadius(const cv::Mat &frame, const GaugeROI &gauge,
                                        double scanRadius, int numAngles = 720) {
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
        intensity[i] = (count > 0) ? (sum / count) : 255;
    }

    int window = 5;
    std::vector<double> smoothed(numAngles, 0);
    for (int i = 0; i < numAngles; i++) {
        double s = 0;
        for (int w = -window; w <= window; w++) {
            int idx = (i + w + numAngles) % numAngles;
            s += intensity[idx];
        }
        smoothed[i] = s / (2.0 * window + 1);
    }

    double globalMin = *std::min_element(smoothed.begin(), smoothed.end());
    double globalMax = *std::max_element(smoothed.begin(), smoothed.end());
    double threshold = globalMin + (globalMax - globalMin) * 0.4;

    std::vector<bool> isMarking(numAngles, false);
    for (int i = 0; i < numAngles; i++) {
        isMarking[i] = (smoothed[i] < threshold);
    }

    std::vector<TickMark> marks;
    int start = -1;
    for (int i = 0; i <= numAngles; i++) {
        bool isMark = (i < numAngles) ? isMarking[i] : false;
        if (isMark && start == -1) {
            start = i;
        } else if (!isMark && start != -1) {
            int end = i - 1;
            if (end - start >= 3) {
                double centerAngle = 2.0 * PI * (start + end) / (2.0 * numAngles);
                double width = 2.0 * PI * (end - start + 1) / numAngles;
                double prominence = 0;
                for (int j = start; j <= end; j++) {
                    prominence += (255.0 - smoothed[j]);
                }
                prominence /= (end - start + 1);
                marks.push_back({centerAngle, width, prominence, scanRadius});
            }
            start = -1;
        }
    }

    return marks;
}

// Cluster markings into groups by angular proximity.
// Returns a vector of aggregated marks (one per cluster).
std::vector<TickMark> clusterMarkings(const std::vector<TickMark> &marks) {
    if (marks.empty()) return {};

    std::vector<TickMark> sorted = marks;
    std::sort(sorted.begin(), sorted.end(),
              [](const TickMark &a, const TickMark &b) { return a.angle < b.angle; });

    // Compute median angular gap to determine clustering threshold
    std::vector<double> gaps;
    for (size_t i = 1; i < sorted.size(); i++) {
        gaps.push_back(sorted[i].angle - sorted[i - 1].angle);
    }
    // Handle wrap-around
    gaps.push_back((2.0 * PI - sorted.back().angle) + sorted.front().angle);

    std::sort(gaps.begin(), gaps.end());
    double medianGap = gaps[gaps.size() / 2];

    // Gap threshold: 3x the median gap separates major groups (numbers)
    double gapThreshold = medianGap * 3.0;

    std::vector<TickMark> clusters;
    double sumAngle = sorted[0].angle * sorted[0].prominence;
    double sumProminence = sorted[0].prominence;
    int clusterCount = 1;

    for (size_t i = 1; i < sorted.size(); i++) {
        double gap = sorted[i].angle - sorted[i - 1].angle;
        if (gap > gapThreshold) {
            double avgAngle = sumAngle / sumProminence;
            clusters.push_back({avgAngle, 0, sumProminence / clusterCount, sorted[i - 1].distance});
            sumAngle = sorted[i].angle * sorted[i].prominence;
            sumProminence = sorted[i].prominence;
            clusterCount = 1;
        } else {
            sumAngle += sorted[i].angle * sorted[i].prominence;
            sumProminence += sorted[i].prominence;
            clusterCount++;
        }
    }

    double avgAngle = sumAngle / sumProminence;
    clusters.push_back({avgAngle, 0, sumProminence / clusterCount, sorted.back().distance});

    return clusters;
}

// Refine detected markings using the even-spacing constraint.
// Removes outliers and fills in undetected markings.
std::vector<TickMark> refineEvenSpacing(const std::vector<TickMark> &marks) {
    if (marks.size() < 3) return marks;

    std::vector<double> angles;
    for (const auto &m : marks) angles.push_back(m.angle);
    std::sort(angles.begin(), angles.end());

    // Compute angular gaps between consecutive markings
    std::vector<double> gaps;
    for (size_t i = 1; i < angles.size(); i++) gaps.push_back(angles[i] - angles[i - 1]);
    gaps.push_back((2.0 * PI - angles.back()) + angles.front());

    // Filter out tiny gaps (noise / duplicate detections)
    double minGap = 1.0 * PI / 180.0;
    gaps.erase(std::remove_if(gaps.begin(), gaps.end(),
                [&](double g) { return g < minGap; }), gaps.end());

    if (gaps.empty()) return marks;

    // Base spacing = median of the remaining gaps
    std::sort(gaps.begin(), gaps.end());
    double baseSpacing = gaps[gaps.size() / 2];
    int totalMarks = cvRound(2.0 * PI / baseSpacing);
    if (totalMarks < 3) return marks;
    baseSpacing = 2.0 * PI / totalMarks;

    // Find the best offset by testing each detected marking
    auto angularDist = [](double a, double b) {
        double d = std::abs(a - b);
        return std::min(d, 2.0 * PI - d);
    };

    int bestCount = 0;
    double bestOffset = angles[0];
    for (auto off : angles) {
        int count = 0;
        for (auto a : angles) {
            double d = angularDist(a, off);
            double residue = std::fmod(d, baseSpacing);
            if (residue < baseSpacing * 0.25 || residue > baseSpacing * 0.75)
                count++;
        }
        if (count > bestCount) {
            bestCount = count;
            bestOffset = off;
        }
    }

    // Generate the full grid of evenly-spaced marking angles
    double avgProminence = 0;
    for (const auto &m : marks) avgProminence += m.prominence;
    avgProminence /= marks.size();

    std::vector<TickMark> refined;
    for (int i = 0; i < totalMarks; i++) {
        double pos = bestOffset + i * baseSpacing;
        pos = std::fmod(pos, 2.0 * PI);
        if (pos < 0) pos += 2.0 * PI;

        // Find the closest detected marking within tolerance
        bool matched = false;
        for (const auto &m : marks) {
            if (angularDist(m.angle, pos) < baseSpacing * 0.3) {
                refined.push_back(m);
                matched = true;
                break;
            }
        }
        if (!matched) {
            // Fill in the missing marking with average prominence
            refined.push_back({pos, 0, avgProminence, marks.front().distance});
        }
    }

    std::sort(refined.begin(), refined.end(),
              [](const TickMark &a, const TickMark &b) { return a.angle < b.angle; });
    return refined;
}

// Given markings from a ring scan, identify the scale endpoints (0 and 4).
// Returns the angles of the first and last major marking clusters.
ScaleCalibration calibrateFromMarkings(const std::vector<TickMark> &marks) {
    ScaleCalibration calib = {0, 0, 0, 4, false};
    if (marks.size() < 4) return calib;

    std::vector<TickMark> clusters = clusterMarkings(marks);
    if (clusters.size() < 2) return calib;

    // Sort clusters by angle
    std::sort(clusters.begin(), clusters.end(),
              [](const TickMark &a, const TickMark &b) { return a.angle < b.angle; });

    // The first and last clusters are the scale endpoints (0 and 4)
    calib.startAngle = clusters.front().angle;
    calib.endAngle = clusters.back().angle;
    calib.minValue = 0;
    calib.maxValue = 4;
    calib.valid = true;

    return calib;
}

// Map the needle angle to a gauge value
double angleToValue(double needleAngle, const ScaleCalibration &scale,
                    const GaugeROI &gauge) {
    if (!scale.valid || needleAngle < 0) return -1;

    // Normalize angles to [0, 2*PI)
    auto normalize = [](double a) {
        a = std::fmod(a, 2.0 * PI);
        if (a < 0) a += 2.0 * PI;
        return a;
    };

    double start = normalize(scale.startAngle);
    double end = normalize(scale.endAngle);
    double needle = normalize(needleAngle);

    // Handle wrap-around (e.g., start at 135°, end at 45°)
    double range;
    if (end > start) {
        range = end - start;
    } else {
        range = (2.0 * PI - start) + end;
    }

    double pos;
    if (end > start) {
        if (needle >= start && needle <= end) {
            pos = (needle - start) / range;
        } else if (needle < start) {
            pos = ((needle + 2.0 * PI) - start) / range;
        } else {
            pos = (needle - start) / range;
        }
    } else {
        if (needle >= start) {
            pos = (needle - start) / range;
        } else if (needle <= end) {
            pos = ((needle + 2.0 * PI) - start) / range;
        } else {
            return -1;
        }
    }

    pos = std::clamp(pos, 0.0, 1.0);
    return scale.minValue + pos * (scale.maxValue - scale.minValue);
}

// Display frame with gauge overlay information
void drawOverlay(cv::Mat &frame, const GaugeROI &gauge,
                 double needleAngle, double value,
                 const ScaleCalibration &scale) {
    if (gauge.radius > 0) {
        cv::circle(frame, gauge.center, gauge.radius, cv::Scalar(0, 255, 0), 2);

        if (scale.valid) {
            // Draw start angle line (green for "0")
            cv::Point startPt(
                gauge.center.x + cvRound(gauge.radius * 0.85 * std::cos(scale.startAngle)),
                gauge.center.y + cvRound(gauge.radius * 0.85 * std::sin(scale.startAngle)));
            cv::line(frame, gauge.center, startPt, cv::Scalar(0, 255, 0), 2);
            cv::putText(frame, "0", startPt + cv::Point(10, 0),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);

            // Draw end angle line (red for "4")
            cv::Point endPt(
                gauge.center.x + cvRound(gauge.radius * 0.85 * std::cos(scale.endAngle)),
                gauge.center.y + cvRound(gauge.radius * 0.85 * std::sin(scale.endAngle)));
            cv::line(frame, gauge.center, endPt, cv::Scalar(0, 0, 255), 2);
            cv::putText(frame, "4", endPt + cv::Point(10, 0),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 0, 255), 2);
        }

        // Draw needle (blue)
        if (needleAngle >= 0) {
            cv::Point needleTip(
                gauge.center.x + cvRound(gauge.radius * 0.8 * std::cos(needleAngle)),
                gauge.center.y + cvRound(gauge.radius * 0.8 * std::sin(needleAngle)));
            cv::line(frame, gauge.center, needleTip, cv::Scalar(255, 0, 0), 3);
            cv::circle(frame, gauge.center, 5, cv::Scalar(255, 0, 0), -1);
        }
    }

    // Draw value text
    std::ostringstream oss;
    oss << "Value: " << std::fixed << std::setprecision(2) << value;
    cv::putText(frame, oss.str(), cv::Point(30, 60),
                cv::FONT_HERSHEY_SIMPLEX, 1.2, cv::Scalar(0, 255, 255), 3);
}

static cv::Point gMarkClick;
static bool gMarkClicked = false;

static void onMarkClick(int event, int x, int y, int flags, void *userdata) {
    if (event == cv::EVENT_LBUTTONDOWN && !gMarkClicked) {
        gMarkClick = cv::Point(x, y);
        gMarkClicked = true;
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path>\n";
        return -1;
    }

    std::string videoPath = argv[1];

    cv::VideoCapture cap(videoPath);
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open video: " << videoPath << "\n";
        return -1;
    }

    double fps = cap.get(cv::CAP_PROP_FPS);
    int totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    std::cout << "Video: " << videoPath << "\n";
    std::cout << "FPS: " << fps << ", Total frames: " << totalFrames << "\n";

    cv::Mat frame;
    if (!cap.read(frame)) {
        std::cerr << "Error: Could not read first frame\n";
        return -1;
    }

    // Step 1: Detect the gauge circle
    std::cout << "Detecting gauge circle...\n";
    std::vector<GaugeROI> gauges = detectGaugeCircles(frame);
    if (gauges.empty()) {
        std::cerr << "Error: No gauge circle detected.\n";
        return -1;
    }

    GaugeROI gauge = gauges[0];
    std::cout << "  >> Gauge found at center=(" << gauge.center.x << ", "
              << gauge.center.y << "), radius=" << gauge.radius << "\n";

    // Step 2: Calibrate — ask user to click on any one marking on the scale
    ScaleCalibration scale{0, 0, 0, 4, false};

    std::cout << "\nCalibration: click on ANY marking on the gauge scale.\n";
    std::cout << "  (a tick mark, a number, anything on the printed scale)\n";

    cv::namedWindow("Calibration");
    cv::setMouseCallback("Calibration", onMarkClick, nullptr);

    gMarkClicked = false;
    cv::Mat calibFrame = frame.clone();
    cv::circle(calibFrame, gauge.center, gauge.radius, cv::Scalar(0, 255, 0), 2);

    while (!gMarkClicked) {
        cv::Mat disp = calibFrame.clone();
        cv::putText(disp, "Click on any marking on the gauge scale",
                    cv::Point(30, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                    cv::Scalar(0, 255, 255), 2);
        cv::imshow("Calibration", disp);
        int key = cv::waitKey(30);
        if (key == 27 || key == 'q' || key == 'Q') {
            cv::destroyWindow("Calibration");
            return -1;
        }
    }

    cv::circle(calibFrame, gMarkClick, 5, cv::Scalar(0, 0, 255), -1);
    cv::imshow("Calibration", calibFrame);
    cv::waitKey(500);

    double scanRadius = cv::norm(gMarkClick - gauge.center);
    scanRadius = std::clamp(scanRadius, gauge.radius * 0.3, gauge.radius * 0.95);
    std::cout << "  >> Mark clicked at radius " << scanRadius
              << " from center.\n";

    // Step 3: Scan ring at that exact radius to find all markings
    std::cout << "  >> Scanning ring at radius " << scanRadius << "...\n";
    std::vector<TickMark> marks = scanRingAtRadius(frame, gauge, scanRadius, 720);
    std::cout << "  >> Found " << marks.size() << " raw markings.\n";

    // Refine using even-spacing constraint
    std::vector<TickMark> refinedMarks = refineEvenSpacing(marks);
    std::cout << "  >> Refined to " << refinedMarks.size() << " evenly-spaced markings.\n";

    if (refinedMarks.size() >= 4) {
        scale = calibrateFromMarkings(refinedMarks);
    }

    // Draw marking positions for visual feedback (raw vs refined)
    cv::Mat markDisp = frame.clone();
    for (const auto &m : marks) {
        cv::Point pt(
            cvRound(gauge.center.x + scanRadius * std::cos(m.angle)),
            cvRound(gauge.center.y + scanRadius * std::sin(m.angle)));
        cv::circle(markDisp, pt, 3, cv::Scalar(0, 0, 255), -1);  // red = raw
    }
    for (const auto &m : refinedMarks) {
        cv::Point pt(
            cvRound(gauge.center.x + scanRadius * std::cos(m.angle)),
            cvRound(gauge.center.y + scanRadius * std::sin(m.angle)));
        cv::circle(markDisp, pt, 5, cv::Scalar(0, 255, 0), -1);  // green = refined
    }
    cv::putText(markDisp, "Red=raw, Green=refined (evenly spaced). Press any key.",
                cv::Point(30, 30), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                cv::Scalar(0, 255, 255), 2);
    cv::imshow("Calibration", markDisp);
    cv::waitKey(0);
    cv::destroyWindow("Calibration");

    if (!scale.valid) {
        std::cout << "  >> Auto-detection of scale boundaries failed.\n";
        std::cout << "  >> Falling back to manual: click on number 0, then 4.\n";

        gCalibStage = 1;
        gCalibPoints = {cv::Point(0, 0), cv::Point(0, 0), false};

        cv::namedWindow("Manual Calibration");
        cv::setMouseCallback("Manual Calibration", onMouseClick, nullptr);

        cv::Mat manualFrame = frame.clone();
        cv::circle(manualFrame, gauge.center, gauge.radius, cv::Scalar(0, 255, 0), 2);

        while (gCalibStage < 3) {
            cv::Mat disp = manualFrame.clone();
            if (gCalibStage == 1) {
                cv::putText(disp, "Click on number '0'", cv::Point(30, 30),
                            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
            } else if (gCalibStage == 2) {
                cv::circle(disp, gCalibPoints.pt0, 5, cv::Scalar(0, 255, 0), -1);
                cv::putText(disp, "0", gCalibPoints.pt0 + cv::Point(10, 0),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
                cv::putText(disp, "Click on number '4'", cv::Point(30, 30),
                            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
            }
            cv::imshow("Manual Calibration", disp);
            int key = cv::waitKey(30);
            if (key == 'c' || key == 'C') break;
        }

        cv::destroyWindow("Manual Calibration");

        if (gCalibPoints.valid) {
            scale.startAngle = std::atan2(gCalibPoints.pt0.y - gauge.center.y,
                                           gCalibPoints.pt0.x - gauge.center.x);
            scale.endAngle = std::atan2(gCalibPoints.pt4.y - gauge.center.y,
                                         gCalibPoints.pt4.x - gauge.center.x);
            scale.minValue = 0;
            scale.maxValue = 4;
            scale.valid = true;
        }
    }

    if (scale.valid) {
        std::cout << "  >> Scale calibrated: 0 at " << (scale.startAngle * 180.0 / PI)
                  << " deg, 4 at " << (scale.endAngle * 180.0 / PI) << " deg\n";
    } else {
        std::cerr << "Calibration failed.\n";
        return -1;
    }

    // Step 4: Process video
    std::cout << "\nProcessing video...\n";

    cv::VideoWriter writer;
    std::string outputPath = videoPath.substr(0, videoPath.find_last_of('.')) + "_output.avi";
    int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
    writer.open(outputPath, fourcc, fps, frame.size());
    if (!writer.isOpened()) {
        std::cerr << "Warning: Could not open output video writer\n";
    }

    std::deque<double> valueHistory;
    const int smoothWindow = 5;

    cap.set(cv::CAP_PROP_POS_FRAMES, 0);

    int frameCount = 0;
    while (true) {
        if (!cap.read(frame)) break;

        double needleAngle = detectNeedleAngle(frame, gauge);
        double value = angleToValue(needleAngle, scale, gauge);

        if (value >= 0) {
            valueHistory.push_back(value);
            if (valueHistory.size() > smoothWindow) {
                valueHistory.pop_front();
            }
        }

        double smoothValue = value;
        if (!valueHistory.empty()) {
            smoothValue = std::accumulate(valueHistory.begin(), valueHistory.end(), 0.0)
                          / valueHistory.size();
        }

        drawOverlay(frame, gauge, needleAngle, smoothValue, scale);

        if (writer.isOpened()) {
            writer.write(frame);
        }

        cv::imshow("Gauge Reader", frame);

        int key = cv::waitKey(1);
        if (key == 27 || key == 'q' || key == 'Q') break;

        frameCount++;
        if (frameCount % 30 == 0) {
            std::cout << "\r  Frame " << frameCount << "/" << totalFrames
                      << " | Value: " << std::fixed << std::setprecision(2) << smoothValue
                      << std::flush;
        }
    }

    std::cout << "\n\nDone! " << frameCount << " frames processed.\n";
    if (writer.isOpened()) {
        std::cout << "Output saved to: " << outputPath << "\n";
    }

    if (!valueHistory.empty()) {
        double finalValue = std::accumulate(valueHistory.begin(), valueHistory.end(), 0.0)
                            / valueHistory.size();
        std::cout << "Final reading: " << std::fixed << std::setprecision(3)
                  << finalValue << "\n";
    }

    cap.release();
    if (writer.isOpened()) writer.release();
    cv::destroyAllWindows();

    return 0;
}
