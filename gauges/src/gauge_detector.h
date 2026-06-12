#pragma once

#include <opencv2/opencv.hpp>
#include <vector>
#include <deque>

struct GaugeROI {
    cv::Point center;
    int radius;
};

struct ScaleCalibration {
    double startAngle;
    double endAngle;
    double minValue;
    double maxValue;
    bool valid;
};

struct TickMark {
    double angle;
    double angularWidth;
    double prominence;
    double distance;
};

inline constexpr double PI = 3.14159265358979323846;

enum class GaugeState {
    INIT,
    CIRCLE_MANUAL,
    CALIB_MIN,
    CALIB_MAX,
    CALIB_CONFIRM,
    PROCESSING
};

class GaugeDetector {
public:
    // ─── Static: Find all gauges in a frame ─────────────────────────
    static std::vector<GaugeROI> findGauges(const cv::Mat &frame,
                                             int cannyThreshold,
                                             int accumulatorThreshold);

    // ─── State Machine ──────────────────────────────────────────────
    GaugeState state() const { return m_state; }
    void setState(GaugeState s) { m_state = s; }

    int circleStage() const { return m_circleStage; }
    void setCircleStage(int stage) { m_circleStage = stage; }
    const cv::Point &circleCenter() const { return m_circleCenter; }
    void setCircleCenter(const cv::Point &pt) { m_circleCenter = pt; }
    int circleRadius() const { return m_circleRadius; }
    void setCircleRadius(int r) { m_circleRadius = r; }

    const cv::Point &ptMin() const { return m_ptMin; }
    void setPtMin(const cv::Point &pt) { m_ptMin = pt; }
    const cv::Point &ptMax() const { return m_ptMax; }
    void setPtMax(const cv::Point &pt) { m_ptMax = pt; }

    int calibTrackMin() const { return m_calibTrackMin; }
    void setCalibTrackMin(int v) { m_calibTrackMin = v; }
    int calibTrackMax() const { return m_calibTrackMax; }
    void setCalibTrackMax(int v) { m_calibTrackMax = v; }

    // ─── Circle Detection ───────────────────────────────────────────
    bool detectCircle(const cv::Mat &frame);
    void setCircle(const cv::Point &center, int radius);

    // ─── Needle Detection ───────────────────────────────────────────
    double detectNeedle(const cv::Mat &frame);

    // ─── Calibration ────────────────────────────────────────────────
    void calibrateFromPoints(const cv::Point &ptMin, const cv::Point &ptMax);
    void setCalibrationValues(double minVal, double maxVal);
    void setCalibrationValid(bool valid);
    void setStartAngle(double a);
    void setEndAngle(double a);

    // ─── Value Mapping ──────────────────────────────────────────────
    double angleToValue(double needleAngle) const;

    // ─── Overlay Drawing ────────────────────────────────────────────
    void drawOverlay(cv::Mat &frame, int labelY = 60) const;

    // ─── State Access ───────────────────────────────────────────────
    const GaugeROI &gauge() const { return m_gauge; }
    const ScaleCalibration &scale() const { return m_scale; }
    bool hasGauge() const { return m_gauge.radius > 0; }
    bool hasScale() const { return m_scale.valid; }

    // ─── Ring Scan (static utilities) ──────────────────────────────
    static std::vector<TickMark> scanRingAtRadius(const cv::Mat &frame,
                                                   const GaugeROI &gauge,
                                                   double scanRadius,
                                                   int numAngles = 720);
    static std::vector<TickMark> refineEvenSpacing(const std::vector<TickMark> &marks);

    // ─── Smoothing ──────────────────────────────────────────────────
    double smoothReadings(double value);
    double getSmoothedValue() const;
    void resetSmoothing() {
        valueHistory.clear();
        smoothedValue = 0;
    }

    // ─── Reset ──────────────────────────────────────────────────────
    void reset();

private:
    GaugeROI m_gauge{};
    ScaleCalibration m_scale{0, 0, 0, 1000, false};

    // State machine
    GaugeState m_state = GaugeState::INIT;
    int m_circleStage = 0;
    cv::Point m_circleCenter;
    int m_circleRadius = 0;
    cv::Point m_ptMin, m_ptMax;
    int m_calibTrackMin = 0;
    int m_calibTrackMax = 1000;

    cv::Mat createMask(const cv::Mat &frame) const;
    double detectColoredNeedle(const cv::Mat &frame) const;
    double detectNeedleRadial(const cv::Mat &frame) const;

    std::deque<double> valueHistory;
    const int smoothWindow = 5;
    double angle = -1;
    double value = -1;
    double smoothedValue = 0;
};
