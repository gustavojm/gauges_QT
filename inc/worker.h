#pragma once

#include <QBasicTimer>
#include <QImage>
#include <QObject>
#include <QString>
#include <QTimerEvent>
#include <QVector>

#include <opencv2/opencv.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "circular_gauge.h"
#include "edgewise_gauge.h"
#include "gauge.h"

/**
 * @struct GaugeCalibData
 * @brief Calibration and live-value data for a single gauge.
 *
 * Transferred between the Worker thread and the GUI via queued signals.
 * Also used for alarm table logging.
 */
struct GaugeCalibData {
    std::optional<double> value = std::nullopt;          ///< Current smoothed gauge value.
    double min_value = 0;                                ///< Calibrated minimum value.
    double max_value = 1000;                             ///< Calibrated maximum value.
    uint32_t color_rgb = 0x00FF00;                       ///< Gauge colour as packed RGB.
    bool alarm_enabled = false;                          ///< Whether the alarm is active.
    AlarmDirection alarm_direction = AlarmDirection::kGreaterThan; ///< Alarm comparison direction.
    double alarm_threshold = 0;                          ///< Alarm threshold value.
    bool alarm_triggered = false;                        ///< Whether the alarm is currently triggered.
    QString tag;                                         ///< User-defined instrument tag.
};

Q_DECLARE_METATYPE(GaugeCalibData)

/**
 * @struct DetectionState
 * @brief Transient state maintained during the detection phase.
 *
 * Holds the list of detected gauges, the active gauge type,
 * manual-placement data, and the current Canny threshold.
 */
struct DetectionState {
    std::vector<std::unique_ptr<Gauge>> gauges;  ///< Detected gauge objects (not yet confirmed).
    GaugeType activeType = GaugeType::kCircular;  ///< Currently selected gauge type for detection.
    bool manualPlacement = false;                 ///< True when in manual placement mode.
    int canny = 320;                             ///< Current Canny edge threshold.
    std::vector<cv::Point> manualEdges;           ///< Edge points clicked during manual placement.
};

/**
 * @struct CalibrationState
 * @brief Transient state maintained during the calibration phase.
 *
 * Tracks which gauge and marker the user is currently dragging.
 */
struct CalibrationState {
    int draggingGaugeIdx = -1;                          ///< Index of the gauge being dragged, or -1.
    CalibrationMarker draggingMarker = CalibrationMarker::kNone; ///< Which marker is being dragged.
};

/**
 * @class Worker
 * @brief Background processing worker running on its own QThread.
 *
 * Drives the video processing pipeline: detection, calibration, and
 * frame-by-frame needle tracking. Communicates with the GUI exclusively
 * through queued signals/slots — zero shared state, zero mutexes.
 *
 * Frame processing uses a QBasicTimer chain (0 ms interval) that yields
 * to the event loop between frames, keeping the GUI responsive.
 *
 * @see MainWindow
 * @see CircularGauge
 * @see EdgewiseGauge
 */
class Worker : public QObject {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(Worker)

public:
    /**
     * @brief Constructs the worker with a video file path.
     * @param videoPath  Path to the input video file.
     * @param parent     Qt parent object.
     */
    explicit Worker(const std::string& videoPath, QObject* parent = nullptr);

    /**
     * @brief Destructor. Releases the video capture.
     */
    ~Worker() override;

signals:
    /** @signal Emitted when a processed frame is ready for display. */
    void frameReady(const QImage& image);

    /** @signal Emitted when calibration data is ready (entire vector refreshed). */
    void calibrationDataReady(const QVector<GaugeCalibData>& calib);

    /** @signal Emitted when live gauge values are updated during processing. */
    void liveValuesUpdated(const QVector<GaugeCalibData>& calib);

    /** @signal Emitted when the current frame count changes. */
    void frameCountUpdated(int current, int total);

    /** @signal Emitted when the number of detected gauges changes. */
    void detectionCountChanged(int numGauges);

    /** @signal Emitted when the operating mode changes. */
    void modeChanged(AppMode mode);

    /** @signal Emitted when manual placement mode is toggled. */
    void manualPlacementActivated(bool active);

    /** @signal Emitted when the manual placement instruction stage changes. */
    void manualInstructionChanged(int stage);

    /** @signal Emitted when the active gauge type is changed. */
    void gaugeTypeChanged(int typeIndex);

    /** @signal Emitted when processing finishes (end of video or quit). */
    void finished();

    /** @signal Emitted when an alarm triggers or clears on a gauge. */
    void alarmTriggered(int gaugeIdx, bool triggered);

public slots:
    /**
     * @slot Opens the video and starts the detection phase.
     *
     * Invoked on the worker thread after QThread::started.
     */
    void start();

    /**
     * @slot Handles a mouse click on the video frame.
     * @param x  X coordinate in image space.
     * @param y  Y coordinate in image space.
     */
    void onImageClicked(int x, int y);

    /**
     * @slot Toggles manual placement mode during detection.
     * @param enabled  True to enter manual placement mode.
     */
    void setManualPlacement(bool enabled);

    /**
     * @slot Sets the active gauge type for automatic detection.
     * @param type  GaugeType::kCircular or kEdgewise.
     */
    void setGaugeType(GaugeType type);

    /**
     * @slot Updates the Canny edge threshold and re-runs detection.
     * @param value  New Canny threshold.
     */
    void setCanny(int value);

    /**
     * @slot Confirms the detected gauges and enters calibration mode.
     */
    void confirmGauges();

    /**
     * @slot Confirms calibration and enters processing mode.
     */
    void confirmCalib();

    /**
     * @slot Sets the calibration value range for a specific gauge.
     * @param idx    0-based gauge index.
     * @param minVal Minimum scale value.
     * @param maxVal Maximum scale value.
     */
    void setGaugeCalibRange(int idx, double minVal, double maxVal);

    /**
     * @slot Enables or disables the alarm for a specific gauge.
     * @param idx      0-based gauge index.
     * @param enabled  True to enable the alarm.
     */
    void setAlarmEnabled(int idx, bool enabled);

    /**
     * @slot Sets the alarm comparison direction for a specific gauge.
     * @param idx        0-based gauge index.
     * @param direction  AlarmDirection value.
     */
    void setAlarmDirection(int idx, AlarmDirection direction);

    /**
     * @slot Sets the alarm threshold for a specific gauge.
     * @param idx        0-based gauge index.
     * @param threshold  Alarm threshold value.
     */
    void setAlarmThreshold(int idx, double threshold);

    /**
     * @slot Sets the user-defined tag for a specific gauge.
     * @param idx  0-based gauge index.
     * @param tag  Instrument tag string.
     */
    void setTag(int idx, const QString& tag);

    /**
     * @slot Handles mouse drag movement for marker adjustment.
     * @param x  X coordinate in image space.
     * @param y  Y coordinate in image space.
     */
    void onDragMove(int x, int y);

    /**
     * @slot Handles mouse release after dragging a marker.
     */
    void onDragRelease();

    /**
     * @slot Restarts video playback from the beginning during processing.
     */
    void restart();

    /**
     * @slot Stops processing, releases resources, and quits the worker thread.
     */
    void quit();

    /**
     * @brief Returns a copy of the current calibration data vector.
     * @return QVector of GaugeCalibData for all confirmed gauges.
     */
    QVector<GaugeCalibData> calib_data() const { return calibData_; }

private:
    /**
     * @brief Qt timer event handler — drives the frame-processing chain.
     * @param event  Timer event.
     */
    void timerEvent(QTimerEvent* event) override;

    /**
     * @brief Processes the next video frame (needle detection, overlay, emit).
     */
    void ProcessNextFrame();

    // Mode-specific click handlers
    /**
     * @brief Handles a click during detection mode (manual placement).
     * @param x  X coordinate.
     * @param y  Y coordinate.
     */
    void HandleDetectionClick(int x, int y);

    /**
     * @brief Handles a click during calibration mode (marker hit-test).
     * @param x  X coordinate.
     * @param y  Y coordinate.
     */
    void HandleCalibrationClick(int x, int y);

    /**
     * @brief Converts an OpenCV BGR Mat to a QImage for display.
     * @param bgr  BGR input image.
     * @return QImage in RGB888 format (deep copy).
     */
    static QImage MatToQImage(const cv::Mat& bgr);

    /**
     * @brief Renders the detection overlay on the first frame and emits frameReady.
     */
    void DisplayDetectionOverlay();

    /**
     * @brief Runs automatic gauge detection on the first frame.
     * @param onlyActiveType  If true, only detect the currently selected type.
     */
    void DetectGauges(bool onlyActiveType = false);

    /**
     * @brief Re-runs detection while preserving manually placed gauges.
     */
    void ReRunDetection();

    /**
     * @brief Publishes the calibration overlay frame and data to the GUI.
     */
    void PublishCalibrationDisplay();

    /**
     * @brief Transitions from detection to calibration mode.
     */
    void EnterCalibration();

    /**
     * @brief Transitions from calibration to processing mode.
     */
    void EnterProcessing();

    /**
     * @brief Rebuilds the calibData_ vector from the current gauge state.
     */
    void RefreshCalibData();

    /**
     * @brief Updates live gauge values and checks alarm conditions.
     */
    void UpdateGaugeValues();

    std::string videoPath_;              ///< Path to the input video file.
    cv::VideoCapture cap_;               ///< OpenCV video capture handle.
    int totalFrames_ = 0;               ///< Total number of frames in the video.
    double fps_ = 0;                    ///< Video frame rate.

    cv::Mat firstFrame_;                ///< First frame (used for detection and calibration display).
    cv::Mat calibFrame_;                ///< Snapshot of the frame when entering calibration.
    std::vector<std::unique_ptr<Gauge>> gauges_; ///< Confirmed gauges (active during calibration/processing).

    DetectionState det_;                ///< Transient detection-phase state.
    CalibrationState cal_;              ///< Transient calibration-phase drag state.

    int frameCount_ = 0;                ///< Number of frames processed so far.
    AppMode mode_ = AppMode::kDetection; ///< Current operating mode.

    QBasicTimer chainTimer_;            ///< Timer driving the frame-processing chain.
    QVector<GaugeCalibData> calibData_; ///< Calibration data vector shared with the GUI.

    bool quit_ = false;                 ///< Set via queued quit() slot to stop the processing loop.

    bool motionInitialized_ = false;    ///< True once motion features have been initialized.
};
