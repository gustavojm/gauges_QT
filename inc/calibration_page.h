#pragma once

#include <QWidget>

#include "gauge_section_helper.h"
#include "circular_gauge.h"
#include "worker.h"

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

/**
 * @class CalibrationPage
 * @brief Control page shown during calibration mode.
 *
 * Displays per-gauge collapsible sections for adjusting min/max calibration
 * markers, alarm settings, and tags.  Provides confirm and cancel buttons
 * to transition to processing mode or back to detection.
 *
 * @see Worker
 * @see ProcessingPage
 */
class CalibrationPage : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(CalibrationPage)
    
public:
    /**
     * @brief Constructs the calibration page.
     * @param parent  Qt parent widget.
     */
    explicit CalibrationPage(QWidget* parent = nullptr);

    /**
     * @brief Connects this page's signals to the Worker's slots.
     * @param worker  Pointer to the Worker instance.
     */
    void connectToWorker(class Worker* worker);

public slots:
    /**
     * @brief Rebuilds the collapsible gauge sections from calibration data.
     * @param calib  Current calibration data vector.
     */
    void onCalibrationDataReady(const QVector<GaugeCalibData>& calib);

signals:
    /// @brief Emitted when the user confirms calibration (enters processing).
    void confirmCalibClicked();

    /// @brief Emitted when the user cancels calibration (returns to detection).
    void cancelCalibClicked();

    /// @brief Emitted when a gauge's calibration range is changed.
    void gaugeCalibRangeChanged(int idx, double minVal, double maxVal);

    /// @brief Emitted when a gauge's alarm enable state changes.
    void alarmEnableChanged(int idx, bool enabled);

    /// @brief Emitted when a gauge's alarm direction changes.
    void alarmDirectionChanged(int idx, AlarmDirection direction);

    /// @brief Emitted when a gauge's alarm threshold changes.
    void alarmThresholdChanged(int idx, double threshold);

    /// @brief Emitted when a gauge's tag is edited.
    void tagChanged(int idx, const QString& tag);

private:
    /**
     * @brief Rebuilds the collapsible section UI from calibration data.
     * @param calib  Current calibration data vector.
     */
    void rebuildCollapsibleSections(const QVector<GaugeCalibData>& calib);

    QLabel* calibInstruction_ = nullptr;       ///< Instruction label for the user.
    QScrollArea* scrollArea_ = nullptr;        ///< Scroll area for the gauge sections.
    QWidget* scrollContent_ = nullptr;         ///< Container widget inside the scroll area.
    QVBoxLayout* sectionsLayout_ = nullptr;    ///< Layout holding the gauge sections.
    std::vector<GaugeSectionWidgets> sections_; ///< Per-gauge UI widget bundles.
    QPushButton* confirmCalibBtn_ = nullptr;   ///< Confirm calibration button.
    QPushButton* cancelCalibBtn_ = nullptr;    ///< Cancel calibration button.
};
