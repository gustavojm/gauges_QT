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
        • VideoWidget mouse click → signal
        • Slider valueChanged → signal
        • Button clicked → signal"]
        UI["Render:
        • QLabel pixmap update from frameReady signal
        • Overlays painted on VideoWidget
        • Mode-specific page in QStackedWidget"]
        SignalFwd["Signal → Worker slot
        (Qt::QueuedConnection)"]
        ProcessSig["Process Worker signals:
        • frameReady → update QLabel
        • gaugeValuesUpdated → update table
        • calibUIUpdated → update spinner ranges
        • modeChanged → switch QStackedWidget page"]

        Init --> Loop
        Loop --> Input --> SignalFwd
        Loop --> UI
        Loop --> ProcessSig
        SignalFwd --> Loop
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
        • FindGauges (HoughCircles)"]
        HandleClick["Slot: handleClick(x, y)
        • Manual circle placement
        • Calibration min/max click"]

        CalibSlots["Slots: confirmGauges / addManual
        / addAnotherGauge / confirmCalib
        / setCalibMin / setCalibMax / startCalibration
        → Modify calibration state machine"]

        ProcessNext["Slot: processNextFrame()
        (triggered by QTimer::singleShot(0))
        • cap.read(next frame)
        • DetectNeedle per gauge
        • DrawOverlay per gauge
        • writer.write(frame)
        • emit frameReady, gaugeValuesUpdated
        • schedule next: QTimer::singleShot(0, …, processNextFrame)"]

        Other["Slots: setCanny / setAcc / setManualPlacement
        → Update detection parameters
        → Re-run FindGauges"]

        Restart["Slot: restart()
        • cap.set(CAP_PROP_POS_FRAMES, 0)
        • Restart detection chain"]
        Quit["Slot: quit()
        • Set quit_ flag
        • QThread::currentThread()->quit()"]

        Emit["emit signals → Main thread:
        • frameReady(QImage)
        • gaugeValuesUpdated(QVector<double>)
        • calibUIUpdated(CalibUIState)
        • modeChanged(AppMode)
        • frameCountUpdated(int, int)
        • detectionUpdated(size_t)"]

        WInit --> CmdLoop
        CmdLoop --> Start --> Emit
        CmdLoop --> HandleClick --> Emit
        CmdLoop --> CalibSlots --> Emit
        CmdLoop --> Other --> Emit
        CmdLoop --> Restart --> Emit
        CmdLoop --> Quit
        ProcessNext -->|"singleShot(0) chain"| Emit
        Emit -->|"Qt queues signal"| CmdLoop
    end

    SignalFwd -.->|"QMetaObject::invokeMethod\n(Qt::QueuedConnection)"| CmdLoop
    Emit -.->|"queued signal"| ProcessSig

    subgraph Mode_Flow["MODE FLOW"]
        D["DETECTION:
        Adjust Canny/Acc sliders
        HoughCircles finds circles
        (auto or manual placement)"]
        C["CALIBRATION:
        Click min marking
        Click max marking
        Set min/max values
        (one or more gauges)"]
        P["PROCESSING:
        QTimer::singleShot chain
        Read & process each frame
        Detect needle angle per gauge
        Display gauge values"]

        D -->|"Confirm / Manual button"| C
        C -->|"Next gauge (auto)"| C
        C -->|"Last gauge done"| P
    end

    D -.->|"emit modeChanged(DETECTION)"| UI
    C -.->|"emit modeChanged(CALIBRATION)"| UI
    P -.->|"emit modeChanged(PROCESSING)"| UI
```

---

## Thread Timeline — How frames flow

```
Worker:  [read N]→[process N]→[emit frameReady(N)]→[next singleShot(0)]→[read N+1]→...
              ↑                          │
          frame N                    Qt queues the signal
              ↓                          ↓
Main:        ...  [processSignal]→[render N]→[next event]→...[process N+1]→[render N+1]→...
```

Worker and Main run in **parallel**. Frames are delivered to the GUI via queued signals — the GUI always shows the latest **completed** frame and never waits for processing.

---

## Key: What runs where

| Operation | Thread | Cost |
|---|---|---|
| `FindGauges` (HoughCircles) | Worker | High |
| `DetectNeedle` (HSV + contours / radial scan) | Worker | High |
| `cap.read` (video I/O) | Worker | Medium |
| `cv::circle`, `cv::putText` (overlays) | Worker | Low |
| `QImage` conversion from `cv::Mat` | Worker | Low |
| `QPixmap::fromImage` + `QLabel::setPixmap` | Main | Low |
| `QSlider`, `QSpinBox`, button event handling | Main | Negligible |
| `QStackedWidget::setCurrentIndex` | Main | Negligible |

The **main thread never blocks** on any high/medium cost operation.

---

## Communication: No shared state

All thread-to-thread communication uses **queued Qt signals/slots**:

| Direction | Mechanism | Example |
|---|---|---|
| Main → Worker | Slot invocation via `QMetaObject::invokeMethod` or signal–slot connection | `invokeMethod(worker_, "setCanny", Qt::QueuedConnection, Q_ARG(int, value))` |
| Worker → Main | Signal emission (auto-queued cross-thread) | `emit frameReady(image)` → Main thread's `onFrameReady` slot |

No mutexes, no shared memory, no polling.

## Mode transitions

Mode changes are signalled from the Worker to Main via `modeChanged(AppMode)`. The Main thread switches the `QStackedWidget` page in response, providing immediate visual feedback. The Worker processes commands asynchronously after the UI has already updated.
