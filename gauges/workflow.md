# Gauge Reader — App Workflow

```mermaid
flowchart TB
    subgraph Main_Thread["MAIN THREAD (GUI)"]
        direction TB
        Init["Init GLFW + ImGui"]
        Loop["Main Loop"]
        Snapshot["Snapshot SharedState
        (lock < 0.1ms)"]
        Tex["Upload texture to GPU"]
        ImGui["Render ImGui:
        • Image with overlays
        • Mode-specific UI
        • Sliders / Buttons / Text"]
        Input["Handle input:
        • Clicks → SharedState
        • Slider changes → SharedState
        • Button presses → Commands"]
        Swap["SwapBuffers + PollEvents"]

        Init --> Loop
        Loop --> Snapshot --> Tex --> ImGui --> Input --> Swap --> Loop
    end

    subgraph Worker_Thread["WORKER THREAD (OpenCV)"]
        direction TB
        WInit["Open VideoCapture
        Read firstFrame"]
        WLoop["Worker Loop"]
        ReadCmd["Read commands from SharedState:
        quit? mode? command?
        clickX/Y? canny? acc?"]

        Detection["DETECTION MODE:
        • FindGauges (HoughCircles)
        • BuildDetectionDisplay
        (draw circles on frame)"]

        Calibration["CALIBRATION MODE:
        • Process pending clicks
        • Auto-transitions
        (circle_stage 3 → CALIB_MIN)
        • Handle ConfirmCalib command
        (calibrate, advance gauge,
        or start Processing)
        • BuildCalibrationDisplay"]

        Processing["PROCESSING MODE:
        • cap.read(next frame)
        • DetectNeedle per gauge
        (colored + radial fallback)
        • DrawOverlay per gauge
        • writer.write(frame)
        • Build display (clone)"]

        Publish["Publish to SharedState:
        • displayFrame (cv::Mat)
        • gaugeValues (vector<double>)
        • detectedGauges
        • calibUI (state machine state)"]

        Sleep["sleep(1ms)"]

        WInit --> WLoop
        WLoop --> ReadCmd
        ReadCmd --> Detection
        ReadCmd --> Calibration
        ReadCmd --> Processing
        Detection --> Publish --> Sleep --> WLoop
        Calibration --> Publish --> Sleep --> WLoop
        Processing --> Publish --> Sleep --> WLoop
    end

    subgraph Shared_State["SharedState (mutex-protected)"]
        direction LR
        W2M["Worker → Main:
        displayFrame
        gaugeValues
        detectedGauges
        calibUI
        frameCount"]
        M2W["Main → Worker:
        quit
        mode (AppMode)
        command (WorkerCommand)
        clickX / clickY
        detectCanny / detectAcc
        calibMinVal / calibMaxVal"]
    end

    Input -.->|writes| M2W
    Snapshot -.->|reads| W2M
    ReadCmd -.->|reads| M2W
    Publish -.->|writes| W2M

    subgraph Mode_Flow["MODE FLOW"]
        D["DETECTION:
        Adjust Canny/Acc sliders
        HoughCircles finds circles"]
        C["CALIBRATION:
        Click min marking
        Click max marking
        Set min/max values"]
        P["PROCESSING:
        Read & process each frame
        Detect needle angle
        Display gauge value"]

        D -->|"Confirm / Manual button"| C
        C -->|"Next gauge (auto)"| C
        C -->|"Last gauge done"| P
    end
```

---

## Thread Timeline — How frames flow

```
Worker:  [read N]→[process N]→[publish]→[read N+1]→[process N+1]→[publish]→...
             ↑                            ↑
         frame N                      frame N+1
             ↓                            ↓
Main:        [snapshot N]→[render N]→[snapshot N+1]→[render N+1]→...
```

Worker and Main run in **parallel**. The main thread always shows the latest **completed** frame and never waits for processing.

---

## Key: What runs where

| Operation | Thread | Cost |
|---|---|---|
| `FindGauges` (HoughCircles) | Worker | High |
| `DetectNeedle` (HSV + contours / radial scan) | Worker | High |
| `cap.read` (video I/O) | Worker | Medium |
| `cv::circle`, `cv::putText` (overlays) | Worker | Low |
| `cv::cvtColor` + `glTexImage2D` | Main | Low |
| `ImGui::Render`, `glfwSwapBuffers` | Main | Low |
| Click handling, slider input | Main | Negligible |

The **main thread never blocks** on any high/medium cost operation.
