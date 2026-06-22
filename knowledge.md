# Gauge Reader — Knowledge File

## Architecture

```
MainWindow (Qt GUI thread)
    │  ┌─ QThread (worker thread)
    │  │   Worker (QObject)
    │  │       │
    │  │       ├── start()               ──→ opens video, runs initial detection
    │  │       ├── setCanny(int)         ──→ re-runs contour detection
    │  │       ├── setAcc(int)           ──→ (unused, kept for API compat)
    │  │       ├── setManualPlacement(bool)
    │  │       ├── handleClick(x, y)     ──→ 5-click manual ellipse placement or calibration click
    │  │       ├── confirmGauges()       ──→ detection → calibration (applies homography)
    │  │       ├── addManual()           ──→ detection → calibration + manual gauge
    │  │       ├── addAnotherGauge()     ──→ adds manual gauge in calibration
    │  │       ├── startCalibration()    ──→ ellipse placement → calibration clicking
    │  │       ├── confirmCalib()        ──→ finish one gauge, advance or start processing
    │  │       ├── setCalibMin/Max(int)  ──→ calibration range values
    │  │       ├── restart()             ──→ reset video to frame 0
    │  │       └── quit()                ──→ set flag, stop timer, exit event loop
    │  │
    │  │   Signals (→ MainWindow):
    │  │       frameReady(QImage)
    │  │       gaugeValuesUpdated(QVector<double>)
    │  │       frameCountUpdated(int, int)
    │  │       detectionUpdated(size_t)
    │  │       calibUIUpdated(CalibUIState)
    │  │       modeChanged(AppMode)
    │  │       finished()                ──→ init failure only
    │  │
    │  └── moveToThread(QThread)
    │
    └── QThread::started → Worker::start
```

### Communication pattern
- **Main → Worker**: queued slot invocations (`QMetaObject::invokeMethod` or signal→slot connections)
- **Worker → Main**: queued signals (auto-detected cross-thread by Qt)
- **No shared state, no mutexes.** The worker owns all processing state as private members, only accessed from the worker thread.
- `VideoWidget::imageClicked` is connected directly to `Worker::handleClick` (cross-thread, auto-queued).

## Key decisions & lessons

### 1. Never poll — use signals/slots
The original design used a `QTimer` at 16ms on the GUI thread reading a `SharedState` struct protected by a mutex. This was replaced in two steps:

- **Step 1**: Worker→Main via signals on a bridge QObject (eliminated GUI-side polling)
- **Step 2**: Converted the worker from a free function + `std::thread` to a `Worker` QObject on a `QThread` (eliminated ALL shared state)

The final design has zero polling and zero mutexes.

### 2. SharedState inevitably becomes a message bus
The original `SharedState` struct grew to hold every field that crossed the thread boundary (mode, clicks, parameters, frame data, calibration state, gauge values, quit flag). This is a code smell — a struct that doubles as an ad-hoc event system.

Replacing it with explicit signals/slots forces clear ownership:
- **Who sends?** The signal's origin.
- **Who receives?** The slot's destination.
- **What data?** Only the relevant parameters, not the entire kitchen sink.

### 3. QObject + QThread vs std::thread
Using QObject + QThread instead of std::thread gives:
- Built-in event loop for queued slot delivery
- Automatic cross-thread signal/slot queuing
- Clean shutdown via `QThread::quit()` + `wait()`
- No manual mutex management

The worker's processing loop (reading video frames) is driven by `QTimer::singleShot(0, ...)` instead of a `while(true)` + `sleep_for`. This yields to the event loop between frames, making the worker responsive to queued commands (quit, restart, etc.) without needing to poll.

### 4. Processing mode is event-driven
In processing mode, each frame is read and processed in response to a `singleShot(0)` timer event. The worker does not block. When the video ends, processing simply stops (no busy-wait, no sleep). The user can restart via `Worker::restart()` which resets the video position and starts the timer chain again.

### 5. Immediate UI feedback for mode transitions
When the user clicks "Confirm" or "Add Manually", the page switches immediately via `setMode()` on the main thread, then the command is forwarded to the worker asynchronously. This keeps the UI responsive while the worker processes the command in its own thread.

### 6. Q_DECLARE_METATYPE must precede MOC
Custom types used in cross-thread signal/slot connections (like `CalibUIState`) need `Q_DECLARE_METATYPE` visible **before** MOC-generated code instantiates the meta-type system. The macro must be placed in a header that is processed early in the translation unit — put it immediately after the struct definition, and include `<QObject>` before it.

## Pipeline Overview
1. **Ellipse detection** — locate the gauge face via contour extraction + `cv::fitEllipse`
2. **Homography computation** — maps fitted ellipse to frontal circle (auto or 5-click manual)
3. **Interactive calibration** — user clicks min/max scale markings, adjusts spinner values
4. **Video processing loop** — optical flow tracks camera movement, homography compensates off-axis distortion, detect needle angle per frame, map to value, overlay + write

## Detection (`CircularGauge::FindGauges`)
- Canny edge detection → dilate → `cv::findContours`
- For each contour with ≥5 points: `cv::fitEllipse`
- Filter: reject elongated ellipses (aspect ratio > 3:1), filter by area bounds
- Compute homography from fitted ellipse via `HomographyFromEllipse`
- Sort by area descending, deduplicate, keep top N
- No HoughCircles — contour + fitEllipse is more robust for elliptical gauges

## Homography (`HomographyFromEllipse`)
From a `cv::RotatedRect` (fitted ellipse):
1. Build conic matrix M from semi-axes and rotation angle
2. Eigendecomposition of M → eigenvalues λ_big, λ_small
3. Eigenvector for λ_small = semi-MAJOR axis direction
4. `H_sub = √λ_big · v_big·v_bigᵀ + √λ_small · v_small·v_smallᵀ`
5. Compose with translation to center circle at `(R, R)` in `2R×2R` output

Same math used for both auto-detected and 5-click manual placement (`ComputeHomography` fits ellipse from points, then same eigendecomposition).

## Manual Placement (5-click)
- User clicks 5 points on the gauge perimeter (no center click)
- `ComputeHomography` fits ellipse via `cv::fitEllipse`, computes center from fitted ellipse
- Homography computed from the fitted ellipse (same path as auto-detection)
- During placement: fitted `cv::RotatedRect` drawn as overlay, edge points shown with connecting polygon

## Optical Flow + Homography (combined)
Both work together for alignment compensation:
- **Optical flow**: `goodFeaturesToTrack` + `calcOpticalFlowPyrLK` tracks feature points inside gauge face
- Computes affine transform (rotation + uniform scale + translation) from reference frame to current
- Updates `roi_.center` and `roi_.radius` based on tracked displacement
- **Homography shift**: `H_new = H_base · T(-dx, -dy)` — only the translation component is adjusted
- Base homography (`homographyBase_`) preserved; warp follows the moved ellipse each frame
- Applied to ALL gauges (both auto-detected and manual)

## Calibration
- User clicks minimum-value marking on the image, then maximum-value marking
- Angles computed in rectified (circle) space via inverse warp of click points
- Markers placed inset along ellipse perimeter (via rectified-space projection)
- Calibration arc drawn by sampling points on circle in rectified space and inverse-warping
- Supports multiple gauges; user calibrates one then advances to the next

## Needle Detection (`CircularGauge::DetectNeedle`)
When homography is active:
- Frame warped to rectified view via `cv::warpPerspective`
- Detection runs on rectified (circular) image
- `detectionCenter()` returns `rectCenter_` (warped coords)

Two methods, tried in order:
### a) Colored needle (`DetectColoredNeedle`)
- HSV segmentation for red (two hue ranges: 0–10 and 160–179)
- Morphological close + open to clean mask
- Find external contours, filter by area and centroid distance
- Score contours by `maxTipDistance × log(area)`
- Return angle from center to farthest contour point
### b) Radial scan fallback (`DetectNeedleRadial`)
- Adaptive threshold on masked grayscale
- 360 radial lines from 8% radius to edge
- Score = `density × 0.4 + longestRun/radius × 0.6`

## Angle-to-Value Mapping
- Normalise all angles to `[0, 2π)`, handle wrap-around scale
- Linear interpolation, clamp to `[min, max]`

## Smoothing
- 5-frame moving average via `deque`

## Overlay (`DrawOverlay`)
- When homography active: all elements (circle outline, needle, scale lines, min/max labels) drawn by inverse-warping from rectified space back to original frame
- When no homography: standard circle + needle overlay
- Ellipse outline via `cv::ellipse(img, ellipseRect_, ...)`
- Calibration arc drawn by sampling points on circle in rectified space and inverse-warping (not `cv::ellipse` with mismatched angles)
- Min/max markers placed inset along ellipse via rectified-space projection

## Video Output
- Codec: MJPG → `*_output.avi`

## Build
```
cmake -S /home/gustavo/cpp/gauges_QT -B /home/gustavo/cpp/gauges_QT/build
cmake --build /home/gustavo/cpp/gauges_QT/build
```
Run: `./build/gauges <video_path>`

## Dependencies
- OpenCV 4.x
- Qt6::Widgets
- C++17

## Files
- `src/main.cpp` — MainWindow + VideoWidget implementation, `main()`
- `inc/main_window.h` / `src/main_window.cpp` — MainWindow + VideoWidget declarations
- `inc/worker.h` / `src/worker.cpp` — Worker QObject class + implementation (all processing logic)
- `inc/circular_gauge.h` / `src/circular_gauge.cpp` — CircularGauge class (detection, calibration, needle, overlay, homography, optical flow)
- `inc/detection_page.h` / `src/detection_page.cpp` — Detection UI page
- `inc/calibration_page.h` / `src/calibration_page.cpp` — Calibration UI page
- `inc/processing_page.h` / `src/processing_page.cpp` — Processing UI page
- `CMakeLists.txt` — Build config (OpenCV + Qt6::Widgets)
