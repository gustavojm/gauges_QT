#pragma once

#include "gauge.h"
#include <QWidget>

class QComboBox;
class QSlider;
class QLabel;
class QPushButton;

/**
 * @class DetectionPage
 * @brief Control page shown during the detection phase.
 *
 * Provides controls for gauge type selection (circular / edgewise),
 * Canny threshold adjustment, manual placement toggle, gauge count
 * display, and a confirm button to transition to calibration.
 *
 * @see Worker
 * @see CalibrationPage
 */
class DetectionPage : public QWidget {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(DetectionPage)
    
public:
    /**
     * @brief Constructs the detection page.
     * @param parent  Qt parent widget.
     */
    explicit DetectionPage(QWidget* parent = nullptr);

    /**
     * @brief Connects this page's signals to the Worker's slots.
     * @param worker  Pointer to the Worker instance.
     */
    void ConnectToWorker(class Worker* worker);

public slots:
    /**
     * @slot Updates the detected gauge count label.
     * @param numGauges  Number of currently detected gauges.
     */
    void onDetectionCountChanged(int numGauges);

    /**
     * @slot Shows or hides the manual-placement instruction label.
     * @param active  True when manual placement is active.
     */
    void onManualPlacementActivated(bool active);

    /**
     * @slot Updates the manual-placement instruction text.
     * @param stage  Current placement stage (0-based).
     */
    void onManualInstructionChanged(int stage);

signals:
    /** @signal Emitted when the manual placement button is toggled. */
    void manualPlacementToggled(bool checked);

    /** @signal Emitted when the gauge type combo box changes. */
    void gaugeTypeChanged(GaugeType type);

    /** @signal Emitted when the Canny slider value changes. */
    void cannyChanged(int value);

    /** @signal Emitted when the user confirms detection (enters calibration). */
    void confirmClicked();

private:
    QPushButton* manualBtn_ = nullptr;          ///< Manual placement toggle button.
    QComboBox* gaugeTypeCombo_ = nullptr;       ///< Gauge type selector (circular / edgewise).
    QWidget* cannyRow_ = nullptr;               ///< Container for the Canny slider row.
    QSlider* cannySlider_ = nullptr;            ///< Canny threshold slider.
    QLabel* cannyValLabel_ = nullptr;           ///< Displays the current Canny value.
    QLabel* gaugeCountLabel_ = nullptr;         ///< Displays the number of detected gauges.
    QLabel* instructionLabel_ = nullptr;        ///< Manual placement instruction text.
    QPushButton* confirmBtn_ = nullptr;         ///< Confirm detection button.
    GaugeType type_ = GaugeType::kCircular;     ///< Currently selected gauge type.
};
