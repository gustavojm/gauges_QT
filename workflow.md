# Gauge Reader — App Workflow

```mermaid
flowchart TB
    subgraph Main_Thread["MAIN THREAD (Qt GUI event loop)"]
        direction TB
        Init["MainWindow created
        Worker + QThread created
        moveToThread(QThread)"]
        Loop["Qt Event Loop
        (QApplication::exec())"]
        Input["User input:
        • VideoWidget mouse click → imageClicked signal
        • VideoWidget mouse drag → mouseMoved signal
        • VideoWidget release → imageMouseReleased signal
        • Slider/checkbox/spinbox → valueChanged signal
        • Button clicked → signal"]
        UI["Render:
        • VideoWidget::setImage → QPainter draws QPixmap
        • mode-specific page shown/hidden via setVisible()
        • Collapsible sections in CalibrationPage / ProcessingPage"]
        ProcessSig["Process Worker signals:
        • frameReady → videoWidget_->setImage
        • modeChanged → setMode() → show/hide pages
        • calibrationDataReady → rebuild collapsible sections
        • liveValuesUpdated → update section titles
        • frameCountUpdated → update frame counter label
        • detectionCountChanged → update gauge count label
        • manualPlacementActivated → hide sliders, show instructions
        • manualInstructionChanged → update manual click step
        • finished → QApplication::quit()"]

        Init --> Loop
        Loop --> Input -->|"queued invokeMethod"| Worker_Thread
        Loop --> UI
        Loop --> ProcessSig
        UI --> Loop
        ProcessSig --> Loop
    end

    subgraph Worker_Thread["WORKER THREAD (QThread + Worker QObject)"]
        direction TB
        WInit["QThread started
        → Worker::start() invoked via queued connection"]
        CmdLoop["Event loop (QThread::exec)
        Waiting for slot invocations"]

        Start["Slot: start()
        • Open VideoCapture
        • Read first frame
        • FindGauges (HoughCircles)
        • emit frameReady, detectionCountChanged, modeChanged"]

        DetClick["Slot: onImageClicked(x, y)
        → handleDetectionClick():
        • Two-click manual gauge placement
          (center then edge)"]

        CalibClick["Slot: onImageClicked(x, y)
        → handleCalibrationClick():
        • Hit-test min/max markers
        • Start drag on hit marker"]

        Drag["Slots: onDragMove(x,y) / onDragRelease()
        → MoveMarker projects onto circle perimeter
        → publishCalibrationDisplay()"]

        UpdateCanny["Slot: setCanny(int)
        → reRunDetection()
        → emit frameReady, detectionCountChanged"]

        UpdateAcc["Slot: setAcc(int)
        → reRunDetection()
        → emit frameReady, detectionCountChanged"]

        ManualToggle["Slot: setManualPlacement(bool)
        → det_.manualPlacement = checked
        → emit manualPlacementActivated, manualInstructionChanged"]

        ConfirmGauges["Slot: confirmGauges()
        • Create CircularGauge objects from ROIs
        • Store calibFrame_
        • enterCalibration()
          → mode_ = kCalibration
          → emit modeChanged + calibrationDataReady
          → publishCalibrationDisplay()"]

        SetRange["Slot: setGaugeCalibRange(idx, min, max)
        → gauge.SetCalibrationValues(min, max)
        → refreshCalibData() → emit calibrationDataReady"]

        ConfirmCalib["Slot: confirmCalib()
        → FinalizeCalibration() per gauge (atan2)
        → enterProcessing()
          → cap.set(CAP_PROP_POS_FRAMES, 0)
          → emit modeChanged + calibrationDataReady
          → frameCount_ = 0
          → chainTimer_.start(0, this)"]

        TimerEv["QBasicTimer::timerEvent()
        → processNextFrame()
          • cap.read(next frame)
          • DetectNeedle per gauge (colored → radial)
          • AngleToValue (linear interp, wrap)
          • AddReading (5-frame smooth)
          • DrawOverlay per gauge
          • emit frameReady
          • emit liveValuesUpdated
          • emit frameCountUpdated
          • chainTimer_.start(0, this) → re-arm"]

        Restart["Slot: restart()
        • cap.set(CAP_PROP_POS_FRAMES, 0)
        • Reset smoothing
        • frameCount_ = 0
        • Restart timer chain"]

        Quit["Slot: quit()
        • quit_ = true
        • chainTimer_.stop()
        • cap_.release()
        • emit finished()
        • QThread::currentThread()->quit()"]

        Emit["emit signals → Main thread:
        • frameReady(QImage)
        • calibrationDataReady(QVector&lt;GaugeCalibData&gt;)
        • liveValuesUpdated(QVector&lt;GaugeCalibData&gt;)
        • frameCountUpdated(int, int)
        • detectionCountChanged(int)
        • modeChanged(AppMode)
        • manualPlacementActivated(bool)
        • manualInstructionChanged(bool)
        • finished()"]

        WInit --> CmdLoop
        CmdLoop --> Start --> Emit
        CmdLoop --> DetClick --> Emit
        CmdLoop --> CalibClick --> Drag --> Emit
        CmdLoop --> UpdateCanny --> Emit
        CmdLoop --> UpdateAcc --> Emit
        CmdLoop --> ManualToggle --> Emit
        CmdLoop --> ConfirmGauges --> Emit
        CmdLoop --> SetRange --> Emit
        CmdLoop --> ConfirmCalib --> Emit
        CmdLoop --> TimerEv -->|"chainTimer_.start(0)"| Emit
        Emit -->|"Qt queues signal"| CmdLoop
        CmdLoop --> Restart --> Emit
        CmdLoop --> Quit
    end

    Main_Thread -.->|"QMetaObject::invokeMethod\n(Qt::QueuedConnection)"| Worker_Thread
    Worker_Thread -.->|"queued signal"| Main_Thread

    subgraph Mode_Flow["MODE FLOW"]
        D["DETECTION:
        Adjust Canny/Acc sliders → re-run HoughCircles
        Check 'manual placement' → 2-click gauge add
        Found N gauges displayed
        Click 'Confirm' when ready"]
        C["CALIBRATION:
        Drag green (min) / red (max) markers on each gauge
        Adjust Min/Max spinbox values per gauge
        Click 'Confirm' to finalize
        Click 'Cancel' to quit"]
        P["PROCESSING:
        QBasicTimer-driven chain (0ms interval)
        Reads & processes every frame sequentially
        Needle detection (colored HSV → radial scan)
        5-frame moving average smoothing
        Live gauge values update per frame
        Click 'Restart' to loop from beginning
        Click 'Quit' to exit"]

        D -->|"confirmGauges()"| C
        C -->|"confirmCalib()"| P
        P -->|"restart()"| P
        P -->|"quit()"| end
        C -->|"cancel → quit()"| end
    end

    D -.->|"modeChanged(DETECTION)"| UI
    C -.->|"modeChanged(CALIBRATION)"| UI
    P -.->|"modeChanged(PROCESSING)"| UI
```

---

## Thread Timeline — How frames flow

```
Worker:  [read N]→[process N]→[emit signals(N)]→[chainTimer_.start(0)]→[read N+1]→...
              ↑                          │
          frame N                    Qt queues the signal
              ↓                          ↓
Main:        [processSignal]→[render N]→[next event]→...[process N+1]→[render N+1]→...
```

Worker and Main run in **parallel**. The `QBasicTimer` chain (0 ms interval) yields to the worker's event loop between frames, allowing queued commands (quit, restart, setCanny, etc.) to be processed immediately. The GUI always shows the latest **completed** frame and never waits for processing.

---

## Key: What runs where

| Operation | Thread | Cost |
|---|---|---|
| `VideoCapture::read` (video I/O) | Worker | Medium |
| `FindGauges` (HoughCircles, 7 param sets × 2 blur variants) | Worker | High |
| `DetectNeedle` (HSV segmentation → radial scan fallback) | Worker | High |
| `DrawOverlay` (circle, lines, text, needle) | Worker | Low |
| `cv::Mat` → `QImage` conversion | Worker | Low |
| `QPixmap::fromImage` + `QPainter` draw | Main | Low |
| `QSlider`, `QSpinBox`, `QCheckBox`, `QPushButton` events | Main | Negligible |
| Collapsible section rebuilding (widget creation) | Main | Low (one-time) |

The **main thread never blocks** on any high/medium cost operation.

---

## Communication: No shared state

All thread-to-thread communication uses **queued Qt signals/slots**:

| Direction | Mechanism | Example |
|---|---|---|
| Main → Worker | Signal–slot connection (auto-queued cross-thread) | `cannySlider → worker.setCanny` |
| Worker → Main | Signal emission (auto-queued cross-thread) | `emit frameReady(image)` → videoWidget lambda |

No mutexes, no shared memory, no polling.

---

## Mode transitions

Mode changes are signalled from the Worker to Main via `modeChanged(AppMode)`. The Main thread shows/hides the corresponding page widget in response, providing immediate visual feedback. The Worker processes commands asynchronously after the UI has already updated.

```
kDetection ──(confirmGauges)──► kCalibration ──(confirmCalib)──► kProcessing
     ▲                                                              │
     │                                                              │
     └──────────────────(restart)───────────────────────────────────┘
```

---

## Calibration details

Each gauge has two markers on its circumference:
- **Min marker** (green) — initial position at 135° (upper-left)
- **Max marker** (red) — initial position at 45° (upper-right)

User drags markers to the actual min/max scale markings on the gauge face, then sets the corresponding numeric values via `QDoubleSpinBox` widgets. `FinalizeCalibration()` computes `start_angle` / `end_angle` via `atan2`. The angle-to-value mapping uses linear interpolation with wrap-around handling.

---

## Shutdown sequence

1. User closes window / presses ESC / clicks Quit
2. `quitRequested()` emitted → `Worker::quit()` sets flag, stops timer, releases `VideoCapture`, calls `QThread::currentThread()->quit()`
3. `workerThread_->wait(3000)` blocks until worker finishes
4. `QThread::finished` → `Worker::deleteLater()`
