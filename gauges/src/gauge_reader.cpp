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

// Detect the gauge circle(s) in the frame using HoughCircles
std::vector<GaugeROI> detectGaugeCircles(const cv::Mat &frame) {
    cv::Mat gray, blurred;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blurred, cv::Size(9, 9), 2, 2);

    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(blurred, circles, cv::HOUGH_GRADIENT, 1,
                     blurred.rows / 8,    // min distance between centers
                     100,                 // canny threshold
                     50,                  // accumulator threshold
                     std::max(blurred.rows, blurred.cols) / 10,  // min radius
                     std::max(blurred.rows, blurred.cols) / 2);  // max radius

    std::vector<GaugeROI> gauges;
    for (const auto &c : circles) {
        gauges.push_back({cv::Point(cvRound(c[0]), cvRound(c[1])), cvRound(c[2])});
    }
    return gauges;
}

// Create a circular mask for the gauge
cv::Mat createGaugeMask(const cv::Mat &frame, const GaugeROI &gauge) {
    cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
    cv::circle(mask, gauge.center, gauge.radius, cv::Scalar(255), -1);
    return mask;
}

// Detect the needle angle using line detection and center-proximity filtering
double detectNeedleAngle(const cv::Mat &frame, const GaugeROI &gauge) {
    cv::Mat gray, blurred, edges;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, blurred, cv::Size(5, 5), 1, 1);

    cv::Mat mask = createGaugeMask(frame, gauge);
    cv::Mat masked;
    gray.copyTo(masked, mask);

    // Use adaptive threshold to handle varying lighting
    cv::Mat thresh;
    cv::adaptiveThreshold(masked, thresh, 255,
                          cv::ADAPTIVE_THRESH_GAUSSIAN_C,
                          cv::THRESH_BINARY_INV, 31, 5);

    // Morphology to clean up noise
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel);
    cv::morphologyEx(thresh, thresh, cv::MORPH_OPEN, kernel);

    cv::Canny(thresh, edges, 50, 150, 3);

    std::vector<cv::Vec4i> lines;
    cv::HoughLinesP(edges, lines, 1, PI / 180, static_cast<int>(gauge.radius * 0.08),
                    static_cast<int>(gauge.radius * 0.1),
                    static_cast<int>(gauge.radius * 0.2));

    if (lines.empty()) {
        return -1;
    }

    // Filter lines that pass close to center and are long enough
    double bestScore = 0;
    double bestAngle = -1;

    for (const auto &line : lines) {
        cv::Point p1(line[0], line[1]);
        cv::Point p2(line[2], line[3]);

        double len = cv::norm(p1 - p2);
        if (len < gauge.radius * 0.7) continue;

        // Find closest point on line to gauge center
        cv::Point diff = p2 - p1;
        double t = ((gauge.center.x - p1.x) * diff.x + (gauge.center.y - p1.y) * diff.y)
                   / (diff.x * diff.x + diff.y * diff.y);
        t = std::clamp(t, 0.0, 1.0);
        cv::Point closest(p1.x + cvRound(t * diff.x), p1.y + cvRound(t * diff.y));
        double distToCenter = cv::norm(closest - gauge.center);

        if (distToCenter > gauge.radius * 0.15) continue;

        // Score: prefer lines that are long, pass close to center, and extend toward edge
        double midX = (p1.x + p2.x) / 2.0;
        double midY = (p1.y + p2.y) / 2.0;
        double distMidToCenter = cv::norm(cv::Point(cvRound(midX), cvRound(midY)) - gauge.center);

        double reach = len * (1.0 - distMidToCenter / gauge.radius);
        double score = reach * (1.0 - distToCenter / gauge.radius);

        if (score > bestScore) {
            bestScore = score;

            // Angle from center to the farthest endpoint of the line
            double d1 = cv::norm(p1 - gauge.center);
            double d2 = cv::norm(p2 - gauge.center);
            cv::Point tip = (d1 > d2) ? p1 : p2;
            bestAngle = std::atan2(tip.y - gauge.center.y, tip.x - gauge.center.x);
        }
    }

    return bestAngle;
}

// Scan around the gauge perimeter to find tick marks and numbers
std::vector<TickMark> scanRingMarkings(const cv::Mat &frame, const GaugeROI &gauge) {
    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    int numAngles = 40;  // 0.5 degree resolution
    double ringInner = gauge.radius * 0.70;
    double ringOuter = gauge.radius * 0.90;
    int ringWidth = cvRound(ringOuter - ringInner);

    std::vector<double> radialIntensity(numAngles, 0);

    for (int i = 0; i < numAngles; i++) {
        double angle = 2.0 * PI * i / numAngles;
        double sum = 0;
        int count = 0;

        for (int r = 0; r < ringWidth; r++) {
            double rad = ringInner + r;
            int x = cvRound(gauge.center.x + rad * std::cos(angle));
            int y = cvRound(gauge.center.y + rad * std::sin(angle));
            if (x >= 0 && x < gray.cols && y >= 0 && y < gray.rows) {
                sum += gray.at<uchar>(y, x);
                count++;
            }
        }
        radialIntensity[i] = (count > 0) ? (sum / count) : 255;
    }

    // Smooth the signal
    std::vector<double> smoothed(numAngles, 0);
    int window = 3;
    for (int i = 0; i < numAngles; i++) {
        double s = 0;
        for (int w = -window; w <= window; w++) {
            int idx = (i + w + numAngles) % numAngles;
            s += radialIntensity[idx];
        }
        smoothed[i] = s / (2 * window + 1);
    }

    // Find valleys (dark markings) by thresholding the smoothed signal
    double globalMin = *std::min_element(smoothed.begin(), smoothed.end());
    double globalMax = *std::max_element(smoothed.begin(), smoothed.end());
    double threshold = globalMin + (globalMax - globalMin) * 0.35;

    std::vector<bool> isMarking(numAngles, false);
    for (int i = 0; i < numAngles; i++) {
        isMarking[i] = (smoothed[i] < threshold);
    }

    // Cluster consecutive marking angles
    std::vector<TickMark> marks;
    int start = -1;
    for (int i = 0; i <= numAngles; i++) {
        bool isMark = (i < numAngles) ? isMarking[i] : false;
        if (isMark && start == -1) {
            start = i;
        } else if (!isMark && start != -1) {
            int end = i - 1;
            if (end - start >= 2) {  // minimum width of 2 angle steps
                double centerAngle = 2.0 * PI * (start + end) / (2.0 * numAngles);
                double width = 2.0 * PI * (end - start + 1) / numAngles;
                double prominence = 0;

                // compute depth of the valley
                for (int j = start; j <= end; j++) {
                    prominence += (255.0 - smoothed[j]);
                }
                prominence /= (end - start + 1);

                marks.push_back({centerAngle, width, prominence, ringInner});
            }
            start = -1;
        }
    }

    return marks;
}

// Auto-detect scale calibration by finding the number "0" and "4" positions
ScaleCalibration autoCalibrateScale(const std::vector<TickMark> &marks,
                                     const GaugeROI &gauge,
                                     int numNumbers = 5) {
    ScaleCalibration calib = {0, 0, 0, 4, false};

    if (marks.size() < 3) return calib;

    // Sort marks by prominence (descending)
    std::vector<TickMark> sortedByProminence = marks;
    std::sort(sortedByProminence.begin(), sortedByProminence.end(),
              [](const TickMark &a, const TickMark &b) {
                  return a.prominence > b.prominence;
              });

    // Take the top markings as likely number positions
    int takeCount = std::min(static_cast<int>(sortedByProminence.size()), numNumbers + 2);
    std::vector<TickMark> topMarks(sortedByProminence.begin(),
                                    sortedByProminence.begin() + takeCount);

    // Sort by angle
    std::sort(topMarks.begin(), topMarks.end(),
              [](const TickMark &a, const TickMark &b) {
                  return a.angle < b.angle;
              });

    if (topMarks.size() < numNumbers) return calib;

    // For a 0-4 gauge, we expect 5 numbers (0,1,2,3,4) evenly distributed.
    // Take the first N major marks as the number positions.
    std::vector<double> numberAngles;
    for (size_t i = 0; i < numNumbers && i < topMarks.size(); i++) {
        numberAngles.push_back(topMarks[i].angle);
    }

    if (numberAngles.size() < 2) return calib;

    // Normalize: sort by angle and handle wrap-around
    std::sort(numberAngles.begin(), numberAngles.end());

    // The first number (0) should be at the start, last number (4) at the end.
    // For the gauge, "0" is the first marking and "4" is the last.
    if (numberAngles.size() < numNumbers) return calib;

    calib.startAngle = numberAngles[0];  // number "0"
    calib.endAngle = numberAngles[numNumbers - 1];  // number "4"
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

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path> [--manual-calib]\n";
        std::cerr << "  --manual-calib : Manually click on numbers 1 and 4\n";
        return -1;
    }

    std::string videoPath = argv[1];
    bool manualCalib = (argc >= 3 && std::string(argv[2]) == "--manual-calib");

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
        std::cerr << "Error: No gauge circle detected. Try adjusting HoughCircles parameters.\n";
        return -1;
    }

    GaugeROI gauge = gauges[0];
    std::cout << "  >> Gauge found at center=(" << gauge.center.x << ", "
              << gauge.center.y << "), radius=" << gauge.radius << "\n";

    // Draw detected circle for verification
    cv::Mat display = frame.clone();
    for (const auto &g : gauges) {
        cv::circle(display, g.center, g.radius, cv::Scalar(0, 255, 0), 2);
    }
    cv::imshow("Gauge Detection", display);
    std::cout << "Press any key to continue...\n";
    cv::waitKey(0);
    cv::destroyWindow("Gauge Detection");

    // Step 2: Calibrate the scale (find where numbers 1 and 4 are)
    ScaleCalibration scale{0, 0, 0, 4, false};

    if (manualCalib) {
        std::cout << "\nManual calibration mode:\n";
        std::cout << "  >> Click on the number '0', then click on the number '4'\n";
        std::cout << "  >> Press 'c' when done.\n";

        gCalibStage = 1;
        gCalibPoints = {cv::Point(0, 0), cv::Point(0, 0), false};

        cv::namedWindow("Calibration");
        cv::setMouseCallback("Calibration", onMouseClick, nullptr);

        cv::Mat calibFrame = frame.clone();
        cv::circle(calibFrame, gauge.center, gauge.radius, cv::Scalar(0, 255, 0), 2);

        while (gCalibStage < 3) {
            cv::Mat calibDisp = calibFrame.clone();

            if (gCalibStage == 1) {
                cv::putText(calibDisp, "Click on number '0'", cv::Point(30, 30),
                            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 255, 0), 2);
            } else if (gCalibStage == 2) {
                cv::circle(calibDisp, gCalibPoints.pt0, 5, cv::Scalar(0, 255, 0), -1);
                cv::putText(calibDisp, "0", gCalibPoints.pt0 + cv::Point(10, 0),
                            cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
                cv::putText(calibDisp, "Click on number '4'", cv::Point(30, 30),
                            cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0, 0, 255), 2);
            }

            cv::imshow("Calibration", calibDisp);
            int key = cv::waitKey(30);
            if (key == 'c' || key == 'C') break;
        }

        cv::destroyWindow("Calibration");

        if (gCalibPoints.valid) {
            scale.startAngle = std::atan2(gCalibPoints.pt0.y - gauge.center.y,
                                           gCalibPoints.pt0.x - gauge.center.x);
            scale.endAngle = std::atan2(gCalibPoints.pt4.y - gauge.center.y,
                                         gCalibPoints.pt4.x - gauge.center.x);
            scale.minValue = 0;
            scale.maxValue = 4;
            scale.valid = true;
            std::cout << "  >> Calibration complete: 0 at angle="
                      << scale.startAngle << " rad, 4 at angle="
                      << scale.endAngle << " rad\n";
        } else {
            std::cout << "  >> Manual calibration cancelled.\n";
        }
    }

    if (!scale.valid) {
        std::cout << "\nAuto-calibrating scale by scanning gauge markings...\n";
        std::vector<TickMark> marks = scanRingMarkings(frame, gauge);
        std::cout << "  >> Found " << marks.size() << " markings\n";

        if (!marks.empty()) {
            scale = autoCalibrateScale(marks, gauge, 5);
            if (scale.valid) {
                std::cout << "  >> Auto-calibration: 1 at angle="
                          << scale.startAngle << " rad, 4 at angle="
                          << scale.endAngle << " rad\n";
            } else {
                std::cout << "  >> Auto-calibration failed, falling back to manual input.\n";
                std::cout << "  >> Defaulting to 135 degrees start, 45 degrees end...\n";
                scale.startAngle = 135.0 * PI / 180.0;
                scale.endAngle = 45.0 * PI / 180.0;
                scale.valid = true;
            }
        } else {
            std::cout << "  >> No markings found, using default angles.\n";
            scale.startAngle = 135.0 * PI / 180.0;
            scale.endAngle = 45.0 * PI / 180.0;
            scale.valid = true;
        }
    }

    // Step 3: Process video
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
