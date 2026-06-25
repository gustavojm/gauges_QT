#pragma once

#include <QWidget>
#include <QVector>

#include "gauge_section_helper.h"
#include "worker.h"

class QLabel;
class QPushButton;
class QVBoxLayout;
class QScrollArea;

/**
 * @class ProcessingPage
 * @brief Control page shown during processing mode.
 *
 * Displays per-gauge collapsible sections with live values, calibration
 * range spin boxes, alarm settings, and tag fields.  Also provides
 * restart and quit buttons.
 *
 * @see Worker
 * @see CalibrationPage
 */
class ProcessingPage : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(ProcessingPage)
    
public:
    /**
     * @brief Constructs the processing page.
     * @param parent  Qt parent widget.
     */
    explicit ProcessingPage(QWidget* parent = nullptr);

    /**
     * @brief Connects this page's signals to the Worker's slots.
     * @param worker  Pointer to the Worker instance.
     */
    void ConnectToWorker(class Worker* worker);

public slots:
    /**
     * @slot Updates the frame-count label.
     * @param current  Current frame number.
     * @param total    Total number of frames.
     */
    void onFrameCountUpdated(int current, int total);

    /**
     * @slot Rebuilds the collapsible gauge sections from calibration data.
     * @param calib  Current calibration data vector.
     */
    void createCollapsibleSections(const QVector<GaugeCalibData>& calib);

    /**
     * @slot Updates the live values displayed in each gauge section.
     * @param calib  Current calibration data vector.
     */
    void onLiveValuesUpdated(const QVector<GaugeCalibData>& calib);

    /**
     * @slot Handles an alarm trigger event for visual feedback.
     * @param gaugeIdx  0-based index of the gauge.
     * @param triggered True if the alarm triggered.
     */
    void onAlarmTriggered(int gaugeIdx, bool triggered);

signals:
    /** @signal Emitted when the user clicks the restart button. */
    void restartClicked();

    /** @signal Emitted when the user clicks the quit button. */
    void quitClicked();

    /** @signal Emitted when a gauge's calibration range is changed. */
    void gaugeCalibRangeChanged(int idx, double minVal, double maxVal);

    /** @signal Emitted when a gauge's alarm enable state changes. */
    void alarmEnableChanged(int idx, bool enabled);

    /** @signal Emitted when a gauge's alarm direction changes. */
    void alarmDirectionChanged(int idx, AlarmDirection direction);

    /** @signal Emitted when a gauge's alarm threshold changes. */
    void alarmThresholdChanged(int idx, double threshold);

    /** @signal Emitted when a gauge's tag is edited. */
    void tagChanged(int idx, const QString& tag);

private:
    QLabel* frameCountLabel_ = nullptr;        ///< Displays current/total frame count.
    QScrollArea* scrollArea_ = nullptr;        ///< Scroll area for the gauge sections.
    QWidget* scrollContent_ = nullptr;         ///< Container widget inside the scroll area.
    QVBoxLayout* sectionsLayout_ = nullptr;    ///< Layout holding the gauge sections.
    std::vector<GaugeSectionWidgets> sections_; ///< Per-gauge UI widget bundles.
    QPushButton* restartBtn_ = nullptr;        ///< Restart button.
    QPushButton* quitBtn_ = nullptr;           ///< Quit button.
};
