#pragma once

#include <QCloseEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QTableWidget>
#include <QPointer>

#include <string>

#include "circular_gauge.h"
#include "worker.h"
#include "video_widget.h"
#include "detection_page.h"
#include "calibration_page.h"
#include "processing_page.h"

/// Maximum number of rows in the alarm log table before old rows are pruned.
inline constexpr int kMaxAlarmRows = 1000;

/**
 * @class MainWindow
 * @brief Main application window — owns the GUI, Worker thread, and page stack.
 *
 * Sets up the video display, control panel (detection / calibration / processing
 * pages), alarm table, and the Worker running on a dedicated QThread.
 * All cross-thread communication uses queued signal/slot connections.
 *
 * @see Worker
 * @see DetectionPage
 * @see CalibrationPage
 * @see ProcessingPage
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
    Q_DISABLE_COPY_MOVE(MainWindow)

public:
    /**
     * @brief Constructs the main window and starts the worker thread.
     * @param videoPath  Path to the input video file.
     */
    explicit MainWindow(const std::string& videoPath);

    /**
     * @brief Destructor. Signals the worker thread to quit and waits.
     */
    ~MainWindow() override;

signals:
    /** @signal Emitted to request the Worker to shut down. */
    void quitRequested();

public slots:
    /**
     * @slot Updates the status bar message.
     * @param message  Status text to display.
     */
    void setStatus(const QString& message);

protected:
    /**
     * @brief Handles the window close event.
     * @param event  Close event (accepted immediately).
     */
    void closeEvent(QCloseEvent* event) override;

    /**
     * @brief Handles key press events (Escape to quit).
     * @param event  Key event.
     */
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    /**
     * @slot Handles an alarm trigger event from the Worker.
     * @param gaugeIdx  0-based index of the gauge.
     * @param triggered True if the alarm just triggered.
     */
    void onAlarmTriggered(int gaugeIdx, bool triggered);

private:
    /**
     * @brief Switches the visible page and updates the status bar.
     * @param mode  Target AppMode.
     */
    void SetMode(AppMode mode);

    QPointer<Worker> worker_ = nullptr;       ///< Pointer to the processing worker.
    QThread* workerThread_ = nullptr;         ///< Dedicated thread for the Worker.

    VideoWidget* videoWidget_ = nullptr;      ///< Video display widget.
    DetectionPage* detectionPage_ = nullptr;  ///< Detection-mode control page.
    CalibrationPage* calibrationPage_ = nullptr; ///< Calibration-mode control page.
    ProcessingPage* processingPage_ = nullptr;   ///< Processing-mode control page.

    AppMode currentMode_ = AppMode::kDetection; ///< Currently active mode.

    QTableWidget* alarmTable_ = nullptr;      ///< Alarm event log table.
};
