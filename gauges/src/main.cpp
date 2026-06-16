#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>

#include "app_state.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "shared_state.h"
#include "worker.h"

constexpr int kImageBottomMargin = 90;
constexpr int kBtnWide = 120;
constexpr int kBtnNarrow = 80;

// ─── GLFW Helper ─────────────────────────────────────────────────

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

// ─── Local UI state snapshot ─────────────────────────────────────

struct UIState {
    AppMode mode = AppMode::kDetection;
    std::vector<double> gaugeValues;
    std::vector<GaugeROI> detectedGauges;
    int frameCount = 0;
    int totalFrames = 0;
    CalibUIState calib;
};

// ─── UI Sections ─────────────────────────────────────────────────

static void RenderDetectionUI(SharedState& shared, const UIState& ui) {
    int canny = 0, acc = 0;
    {
        std::lock_guard<std::mutex> lk(shared.mtx);
        canny = shared.detectCanny;
        acc = shared.detectAcc;
    }

    bool changed = false;
    ImGui::Text("Adjust thresholds until circles are detected:");
    if (ImGui::SliderInt("Canny threshold", &canny, 1, 500)) changed = true;
    if (ImGui::SliderInt("Accumulator threshold", &acc, 1, 500))
        changed = true;
    ImGui::Text("Found %zu gauge(s)", ui.detectedGauges.size());
    ImGui::Spacing();

    if (changed) {
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.detectCanny = canny;
        shared.detectAcc = acc;
        shared.runDetection = true;
    }

    if (!ui.detectedGauges.empty()) {
        if (ImGui::Button("Confirm", ImVec2(kBtnWide, 0))) {
            std::lock_guard<std::mutex> lk(shared.mtx);
            shared.command = WorkerCommand::kConfirmGauges;
            shared.mode = AppMode::kCalibration;
        }
        ImGui::SameLine();
    }

    if (ImGui::Button("Manual", ImVec2(kBtnWide, 0))) {
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.command = WorkerCommand::kStartManual;
        shared.mode = AppMode::kCalibration;
    }
}

static void RenderCalibrationUI(SharedState& shared, const UIState& ui) {
    if (!ui.calib.initialized) return;

    const auto& cal = ui.calib;

    if (cal.state == GaugeState::kCircleManual) {
        if (cal.circleStage == 1)
            ImGui::TextColored(ImVec4(1, 1, 0, 1),
                               "Click on the CENTER of the gauge");
        else
            ImGui::TextColored(ImVec4(1, 1, 0, 1),
                               "Now click on the EDGE of the gauge face");
    }

    if (cal.state == GaugeState::kCalibMin) {
        if (cal.totalGauges > 1)
            ImGui::Text("Gauge %zu / %zu", cal.currentGauge + 1,
                        cal.totalGauges);
        ImGui::TextColored(ImVec4(1, 1, 0, 1),
                           "Click on the MINIMUM value marking");
    }

    if (cal.state == GaugeState::kCalibMax) {
        ImGui::TextColored(ImVec4(1, 1, 0, 1),
                           "Now click on the MAXIMUM value marking");
    }

    if (cal.state == GaugeState::kCalibConfirm) {
        if (cal.totalGauges > 1)
            ImGui::Text("Gauge %zu / %zu", cal.currentGauge + 1,
                        cal.totalGauges);

        int minVal = cal.calibTrackMin;
        int maxVal = cal.calibTrackMax;
        if (ImGui::SliderInt("Min value", &minVal, 0, 1000) ||
            ImGui::SliderInt("Max value", &maxVal, 0, 1000)) {
            std::lock_guard<std::mutex> lk(shared.mtx);
            shared.calibMinVal = minVal;
            shared.calibMaxVal = maxVal;
        }
        ImGui::Text("Min = %d   Max = %d", minVal, maxVal);
        ImGui::Spacing();

        if (ImGui::Button("Confirm", ImVec2(kBtnWide, 0))) {
            std::lock_guard<std::mutex> lk(shared.mtx);
            shared.command = WorkerCommand::kConfirmCalib;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(kBtnWide, 0))) {
            std::lock_guard<std::mutex> lk(shared.mtx);
            shared.quit = true;
        }
    }
}

static void RenderProcessingUI(SharedState& shared, const UIState& ui) {
    ImGui::Text("Frame %d / %d", ui.frameCount, ui.totalFrames);
    for (size_t i = 0; i < ui.gaugeValues.size(); i++) {
        ImGui::TextColored(ImVec4(0, 1, 1, 1), "Gauge %zu: %.2f", i + 1,
                           ui.gaugeValues[i]);
    }
    ImGui::Spacing();

    if (ImGui::Button("Restart", ImVec2(kBtnNarrow, 0))) {
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.command = WorkerCommand::kRestart;
    }
    ImGui::SameLine();
    if (ImGui::Button("Quit", ImVec2(kBtnNarrow, 0))) {
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.quit = true;
    }
}

// ─── Main ────────────────────────────────────────────────────────

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path>\n";
        return -1;
    }

    std::string videoPath = argv[1];

    // ── Init GLFW + ImGui ──────────────────────────────────────

    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        std::cerr << "Error: Could not initialize GLFW\n";
        return -1;
    }

    GLFWwindow* window =
        glfwCreateWindow(1600, 1000, "Gauge Reader", nullptr, nullptr);
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

    // ── Start worker thread ────────────────────────────────────

    SharedState shared;
    std::thread worker(WorkerMain, videoPath, std::ref(shared));

    VideoTexture videoTex;
    cv::Mat displayFrame;
    UIState ui;

    // ── Main Loop ──────────────────────────────────────────────

    while (!glfwWindowShouldClose(window)) {
        // ── Snapshot latest results from worker ─────────────────
        {
            std::lock_guard<std::mutex> lk(shared.mtx);
            if (shared.quit) break;
            if (shared.frameReady) {
                shared.displayFrame.copyTo(displayFrame);
                shared.frameReady = false;
            }
            ui.mode = shared.mode;
            ui.gaugeValues = shared.gaugeValues;
            ui.detectedGauges = shared.detectedGauges;
            ui.frameCount = shared.frameCount;
            ui.totalFrames = shared.totalFrames;
            ui.calib = shared.calibUI;
        }

        // ── Upload texture ─────────────────────────────────────
        if (!displayFrame.empty()) videoTex.update(displayFrame);

        // ── ImGui Frame ────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Gauge Reader", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ── Image ──────────────────────────────────────────────
        if (!displayFrame.empty()) {
            float availW = ImGui::GetContentRegionAvail().x;
            float availH =
                ImGui::GetContentRegionAvail().y - kImageBottomMargin;
            float imgW, imgH;
            if (displayFrame.cols * availH > displayFrame.rows * availW) {
                imgW = availW;
                imgH = availW * displayFrame.rows / displayFrame.cols;
            } else {
                imgH = availH;
                imgW = availH * displayFrame.cols / displayFrame.rows;
            }
            ImGui::Image((ImTextureID)(intptr_t)videoTex.id,
                         ImVec2(imgW, imgH));
        }

        // ── Handle clicks ──────────────────────────────────────
        int clickX = -1, clickY = -1;
        if (!displayFrame.empty() &&
            TryGetClickOnImage(clickX, clickY, displayFrame.cols,
                               displayFrame.rows)) {
            std::lock_guard<std::mutex> lk(shared.mtx);
            shared.hasClick = true;
            shared.clickX = clickX;
            shared.clickY = clickY;
        }

        // ── Mode-specific UI ───────────────────────────────────
        ImGui::Spacing();

        switch (ui.mode) {
            case AppMode::kDetection:
                RenderDetectionUI(shared, ui);
                break;

            case AppMode::kCalibration:
                RenderCalibrationUI(shared, ui);
                break;

            case AppMode::kProcessing:
                RenderProcessingUI(shared, ui);
                break;
        }

        ImGui::End();

        // ── Render ─────────────────────────────────────────────
        ImGui::Render();
        int fbW, fbH;
        glfwGetFramebufferSize(window, &fbW, &fbH);
        glViewport(0, 0, fbW, fbH);
        glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
        glfwPollEvents();

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            std::lock_guard<std::mutex> lk(shared.mtx);
            shared.quit = true;
        }
    }

    // ── Signal worker and wait ────────────────────────────────
    {
        std::lock_guard<std::mutex> lk(shared.mtx);
        shared.quit = true;
    }
    worker.join();

    // ── Cleanup ───────────────────────────────────────────────
    if (ui.frameCount > 0) {
        std::cout << "\n\nDone! " << ui.frameCount << " frames processed.\n";
    }

    videoTex.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
