    # AGENTS.md — Gauge Reader (Qt6 + OpenCV)

## Build & Run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -Dgauges_ENABLE_CLANG_TIDY=OFF \
  -Dgauges_ENABLE_CPPCHECK=OFF \
  -Dgauges_WARNINGS_AS_ERRORS=OFF \
  -Dgauges_ENABLE_IPO=OFF \
  -Dgauges_ENABLE_HARDENING=OFF \
  -Dgauges_ENABLE_SANITIZER_ADDRESS=OFF \
  -Dgauges_ENABLE_SANITIZER_UNDEFINED=OFF
cmake --build build -j$(nproc)
./build/bin/gauges <video_path>
```

Build directory is `build/`. Uses CMake template from [cpp-best-practices/cmake_template](https://github.com/cpp-best-practices/cmake_template).

**Preset-based build** (uses Ninja):
```bash
cmake --preset unixlike-gcc-debug
cmake --build build
```

**Interactive configuration** (ccmake):
```bash
ccmake -S . -B build        # configure
ccmake --build build         # build from ccmake (press 'b')
```
Use arrow keys to navigate, Enter to toggle, `[C]` to configure, `[G]` to generate.

**Static analysis** (clang-tidy + cppcheck): enable with `-Dgauges_ENABLE_CLANG_TIDY=ON -Dgauges_WARNINGS_AS_ERRORS=OFF`

## Architecture

- **Qt6 + OpenCV**, C++17, no `.ui` files (all code-constructed layouts)
- **Two threads**: MainWindow (GUI) + Worker (QObject on QThread)
- **Zero shared state, zero mutexes** — all cross-thread via queued signals/slots
- Worker drives frame processing via `QBasicTimer` chain (0ms interval, yields to event loop)

## Key Files

| File | Purpose |
|------|---------|
| `src/worker.cpp` | All processing logic, video capture, detection, calibration, needle tracking |
| `src/circular_gauge.cpp` | Ellipse detection, homography, needle detection, overlay drawing |
| `src/edgewise_gauge.cpp` | Panel meter detection, scale strip, needle position |
| `src/main_window.cpp` | GUI setup, alarm table, mode switching |
| `inc/gauge_section_helper.h` | Template function building calibration UI sections |
| `knowledge.md` | Detailed architecture, algorithm explanations, design decisions |
| `workflow.md` | Mermaid diagrams of mode flow and thread timeline |

## Conventions

- **Naming**: PascalCase for functions (`SetCalibrationValues`), snake_case for getters/setters (`min_value()`, `set_alarm_enabled()`)
- **Qt slots** in Worker/Pages keep camelCase (Qt convention)
- **Logging**: `qCritical`/`qWarning`/`qDebug` — no `std::cerr`/`std::cout` (except `main.cpp` for CLI usage)
- **Enums**: Always `enum class` (`AppMode`, `GaugeType`, `AlarmDirection`, `InstrumentOrientation`)
- **Constants**: `inline constexpr` with descriptive names — no magic numbers
- **Headers**: `#pragma once`, self-contained

## Pitfalls

- `Q_DECLARE_METATYPE` for custom types must appear before MOC processes them
- Qt `new` for widgets is intentional — parent-child ownership manages lifetime (don't use smart pointers for Qt widgets)
- `cv::invert()` return value must be checked (returns 0 on failure)
- Worker thread runs on `QBasicTimer` chain — not `while(true)` + sleep

## C++ Standards

Follow C++ Core Guidelines. See `cpp-coding-standards` skill for details. Key rules:
- RAII everywhere, smart pointers for non-Qt ownership
- `const`/`constexpr` by default
- `enum class` only
- No C-style casts
