#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <opencv2/opencv.hpp>
#include <string>

#include "app_state.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

constexpr int kCircleThickness = 2;
constexpr int kLabelYInit = 60;
constexpr int kLabelYStep = 30;
constexpr int kImageBottomMargin = 70;
constexpr int kBtnWide = 120;
constexpr int kBtnNarrow = 80;
constexpr int kWindowPadW = 100;
constexpr int kWindowPadH = 200;
constexpr int kWindowMaxW = 1600;
constexpr int kWindowMaxH = 1000;

// ─── GLFW/ImGui Helpers ───────────────────────────────────────────

static void GlfwErrorCallback(int error, const char* description) {
    std::cerr << "GLFW Error " << error << ": " << description << "\n";
}

struct VideoTexture {
    GLuint id = 0;
    int w = 0, h = 0;

    void update(const cv::Mat& bgr) {
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        if (id == 0) glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (bgr.cols != w || bgr.rows != h) {
            w = bgr.cols;
            h = bgr.rows;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB,
                         GL_UNSIGNED_BYTE, rgb.data);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGB,
                            GL_UNSIGNED_BYTE, rgb.data);
        }
    }

    void destroy() {
        if (id) {
            glDeleteTextures(1, &id);
            id = 0;
            w = h = 0;
        }
    }
};

static bool TryGetClickOnImage(int& outX, int& outY, int imageW, int imageH) {
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
        ImVec2 mpos = ImGui::GetMousePos();
        ImVec2 min = ImGui::GetItemRectMin();
        ImVec2 max = ImGui::GetItemRectMax();
        float dispW = max.x - min.x;
        float dispH = max.y - min.y;
        outX = static_cast<int>((mpos.x - min.x) / dispW * imageW);
        outY = static_cast<int>((mpos.y - min.y) / dispH * imageH);
        return true;
    }
    return false;
}

// ─── Application Logic ─────────────────────────────────────────────

static void ProcessTransitions(AppState& app, const cv::Mat& frame) {
    switch (app.mode) {
        case AppMode::kDetection:
            if (app.detectCanny != app.prevCanny ||
                app.detectAcc != app.prevAcc) {
                app.detectedGauges = GaugeDetector::FindGauges(
                    frame, app.detectCanny, app.detectAcc);
                app.prevCanny = app.detectCanny;
                app.prevAcc = app.detectAcc;
            }
            break;

        case AppMode::kCalibration: {
            // Manual circle → CALIB_MIN transition
            if (app.detectedGauges.empty() && !app.detectors.empty()) {
                auto& d = app.detectors[0];
                if (d.state() == GaugeState::kCircleManual &&
                    d.circle_stage() == 3 && d.circle_radius() > 0) {
                    d.SetCircle(d.circle_center(), d.circle_radius());
                    const auto& g = d.gauge();
                    std::cout << "  >> Gauge at (" << g.center.x << ", "
                              << g.center.y << "), radius=" << g.radius << "\n";
                    app.calibFrame = frame.clone();
                    cv::circle(app.calibFrame, g.center, g.radius, d.color(),
                               2);
                    d.set_state(GaugeState::kCalibMin);
                }
            }
            // All detectors done calibrating → enter Processing mode
            if (!app.detectors.empty()) {
                bool allDone = true;
                for (const auto& det : app.detectors)
                    if (det.state() != GaugeState::kProcessing) {
                        allDone = false;
                        break;
                    }
                if (allDone) app.mode = AppMode::kProcessing;
            }
            break;
        }

        case AppMode::kProcessing:
            break;

        default:
            break;
    }
}

// ─── Display ────────────────────────────────────────────────────────

static cv::Mat BuildDisplayImage(const AppState& app, const cv::Mat& frame) {
    switch (app.mode) {
        case AppMode::kDetection: {
            cv::Mat disp = frame.clone();
            static const std::vector<cv::Scalar> previewPalette = {
                {0, 0, 255},   {255, 0, 0},   {0, 255, 255},
                {255, 0, 255}, {255, 255, 0}, {0, 165, 255},
            };
            for (size_t i = 0; i < app.detectedGauges.size(); i++) {
                const auto& c = app.detectedGauges[i];
                const auto& col = previewPalette[i % previewPalette.size()];
                cv::circle(disp, c.center, c.radius, col, 2);
                cv::putText(disp, std::to_string(c.radius),
                            c.center + cv::Point(-20, 5),
                            cv::FONT_HERSHEY_SIMPLEX, 0.5, col, 1);
            }
            return disp;
        }
        case AppMode::kProcessing:
            return frame.clone();
        case AppMode::kCalibration: {
            if (!app.calibFrame.empty()) {
                cv::Mat disp = app.calibFrame.clone();
                size_t idx =
                    app.detectedGauges.empty() ? 0 : app.currentGaugeIdx;
                const auto& d = app.detectors[idx];
                const auto& g = d.gauge();
                cv::circle(disp, g.center, g.radius, d.color(), 2);
                if (d.state() == GaugeState::kCalibMax) {
                    cv::circle(disp, d.pt_min(), 10, cv::Scalar(0, 255, 255),
                               2);
                } else if (d.state() == GaugeState::kCalibConfirm) {
                    cv::circle(disp, d.pt_min(), 10, cv::Scalar(0, 255, 0), 2);
                    cv::circle(disp, d.pt_max(), 10, cv::Scalar(0, 0, 255), 2);
                }
                return disp;
            }
            cv::Mat disp = frame.clone();
            if (!app.detectors.empty()) {
                const auto& d = app.detectors[0];
                if (d.state() == GaugeState::kCircleManual &&
                    d.circle_stage() == 2) {
                    cv::circle(disp, d.circle_center(), 5,
                               cv::Scalar(0, 255, 0), -1);
                    cv::circle(disp, d.circle_center(), 30,
                               cv::Scalar(0, 255, 0), 1);
                }
            }
            return disp;
        }

        default:
            return frame.clone();
    }
}

// ─── UI Sections ────────────────────────────────────────────────────

static void RenderDetectionUI(AppState& app, cv::Mat& frame) {
    ImGui::Text("Adjust thresholds until circles are detected:");
    ImGui::SliderInt("Canny threshold", &app.detectCanny, 1, 500);
    ImGui::SliderInt("Accumulator threshold", &app.detectAcc, 1, 500);
    ImGui::Text("Found %zu gauge(s)", app.detectedGauges.size());
    ImGui::Spacing();

    if (!app.detectedGauges.empty()) {
        if (ImGui::Button("Confirm", ImVec2(kBtnWide, 0))) {
            app.detectors.clear();
            for (size_t i = 0; i < app.detectedGauges.size(); ++i) {
                app.detectors.emplace_back(app.detectedGauges[i].center,
                                           app.detectedGauges[i].radius,
                                           GaugeDetector::NextColor());
                std::cout << "  >> Gauge " << i << " at ("
                          << app.detectedGauges[i].center.x << ", "
                          << app.detectedGauges[i].center.y
                          << "), radius=" << app.detectedGauges[i].radius
                          << "\n";
            }
            app.calibFrame = frame.clone();
            app.currentGaugeIdx = 0;
            app.mode = AppMode::kCalibration;
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("Manual", ImVec2(kBtnWide, 0))) {
        std::cout << "  >> Switching to manual circle placement.\n";
        std::cout << "  >> Click center, then edge.\n";
        app.detectors.emplace_back();
        app.detectors[0].set_color(GaugeDetector::NextColor());
        app.detectors[0].set_state(GaugeState::kCircleManual);
        app.detectors[0].set_circle_stage(1);
        app.mode = AppMode::kCalibration;
    }
}

static void RenderProcessingUI(AppState& app) {
    ImGui::Text("Frame %d / %d", app.frameCount, app.totalFrames);
    for (size_t i = 0; i < app.detectors.size(); i++) {
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Gauge %zu: %.2f", i + 1,
                           app.detectors[i].GetSmoothedValue());
    }
    ImGui::Spacing();

    if (ImGui::Button("Restart", ImVec2(kBtnNarrow, 0))) {
        app.cap.set(cv::CAP_PROP_POS_FRAMES, 0);
        for (auto& d : app.detectors) d.ResetSmoothing();
        app.frameCount = 0;
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit", ImVec2(kBtnNarrow, 0))) {
        app.quit = true;
    }
}

// ─── Main ─────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path>\n";
        return -1;
    }

    std::string videoPath = argv[1];

    AppState app;
    app.cap.open(videoPath);
    if (!app.cap.isOpened()) {
        std::cerr << "Error: Could not open video: " << videoPath << "\n";
        return -1;
    }

    app.fps = app.cap.get(cv::CAP_PROP_FPS);
    app.totalFrames = static_cast<int>(app.cap.get(cv::CAP_PROP_FRAME_COUNT));
    std::cout << "Video: " << videoPath << "\n";
    std::cout << "FPS: " << app.fps << ", Total frames: " << app.totalFrames
              << "\n";

    cv::Mat frame;
    if (!app.cap.read(frame)) {
        std::cerr << "Error: Could not read first frame\n";
        return -1;
    }

    // ── Init GLFW + ImGui ──────────────────────────────────────────

    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "Error: Could not initialize GLFW\n";
        return -1;
    }

    int winW = std::min(frame.cols + kWindowPadW, kWindowMaxW);
    int winH = std::min(frame.rows + kWindowPadH, kWindowMaxH);
    GLFWwindow* window =
        glfwCreateWindow(winW, winH, "Gauge Reader", nullptr, nullptr);
    if (!window) {
        std::cerr << "Error: Could not create GLFW window\n";
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    VideoTexture videoTex;

    // ── Main Loop ──────────────────────────────────────────────────

    while (!glfwWindowShouldClose(window) && !app.quit) {
        // ── Frame acquisition (processing mode only) ────────────────
        if (app.mode == AppMode::kProcessing) {
            if (!app.cap.read(frame)) break;
        }

        // ── State transitions ───────────────────────────────────────
        ProcessTransitions(app, frame);

        // ── Processing work ──────────────────────────────────────────
        if (app.mode == AppMode::kProcessing) {
            int labelY = kLabelYInit;
            for (auto& d : app.detectors) {
                d.DetectNeedle(frame);
                d.DrawOverlay(frame, labelY);
                labelY += kLabelYStep;
            }

            if (app.writer.isOpened()) app.writer.write(frame);
        }

        // ── Build display image ──────────────────────────────────────
        cv::Mat disp = BuildDisplayImage(app, frame);

        // ── Upload texture ───────────────────────────────────────────
        videoTex.update(disp);

        // ── ImGui Frame ──────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Gauge Reader", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ── Image ────────────────────────────────────────────────────
        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y - kImageBottomMargin;
        float imgW, imgH;
        if (disp.cols * availH > disp.rows * availW) {
            imgW = availW;
            imgH = availW * disp.rows / disp.cols;
        } else {
            imgH = availH;
            imgW = availH * disp.cols / disp.rows;
        }
        ImGui::Image((ImTextureID)(intptr_t)videoTex.id, ImVec2(imgW, imgH));

        // ── Handle clicks ────────────────────────────────────────────
        int clickX = -1, clickY = -1;
        if (TryGetClickOnImage(clickX, clickY, disp.cols, disp.rows)) {
            if (!app.detectors.empty()) {
                size_t idx =
                    app.detectedGauges.empty() ? 0 : app.currentGaugeIdx;
                app.detectors[idx].HandleClick(clickX, clickY);
            }
        }

        // ── Mode-specific UI ─────────────────────────────────────────
        ImGui::Spacing();

        switch (app.mode) {
            case AppMode::kDetection:
                RenderDetectionUI(app, frame);
                break;

            case AppMode::kCalibration:
                if (!app.detectors.empty()) {
                    size_t idx =
                        app.detectedGauges.empty() ? 0 : app.currentGaugeIdx;
                    if (app.detectors[idx].RenderCalibrationUI(
                            idx, app, videoPath, frame))
                        app.quit = true;
                }
                break;

            case AppMode::kProcessing:
                RenderProcessingUI(app);
                break;

            default:
                break;
        }

        if (app.mode == AppMode::kProcessing) app.frameCount++;

        ImGui::End();

        // ── Render ───────────────────────────────────────────────────
        ImGui::Render();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) break;
    }

    // ── Cleanup ─────────────────────────────────────────────────────

    if (app.frameCount > 0) {
        std::cout << "\n\nDone! " << app.frameCount << " frames processed.\n";
    }

    videoTex.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    app.cap.release();
    if (app.writer.isOpened()) app.writer.release();

    return 0;
}
