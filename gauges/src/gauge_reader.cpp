#include "gauge_detector.h"

#include <opencv2/opencv.hpp>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <iostream>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <deque>

// ─── Globals (GUI state shared across callbacks) ─────────────────

static cv::Point gCircleCenter;
static int gCircleRadius = 0;
static int gCircleStage = 0;

static int gCalibTrackMin = 0;
static int gCalibTrackMax = 1000;
static int gCalibPhase = 0;
static cv::Point gPtMin, gPtMax;

// ─── GLFW/ImGui Helpers ───────────────────────────────────────────

static void glfw_error_callback(int error, const char *description) {
    std::cerr << "GLFW Error " << error << ": " << description << "\n";
}

struct VideoTexture {
    GLuint id = 0;
    int w = 0, h = 0;

    void update(const cv::Mat &bgr) {
        cv::Mat rgb;
        cv::cvtColor(bgr, rgb, cv::COLOR_BGR2RGB);
        if (id == 0) glGenTextures(1, &id);
        glBindTexture(GL_TEXTURE_2D, id);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (bgr.cols != w || bgr.rows != h) {
            w = bgr.cols; h = bgr.rows;
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0,
                         GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h,
                            GL_RGB, GL_UNSIGNED_BYTE, rgb.data);
        }
    }

    void destroy() {
        if (id) { glDeleteTextures(1, &id); id = 0; w = h = 0; }
    }
};

enum AppState {
    STATE_INIT,
    STATE_CIRCLE_MANUAL,
    STATE_CALIB_MIN,
    STATE_CALIB_MAX,
    STATE_CALIB_CONFIRM,
    STATE_PROCESSING
};

static bool tryGetClickOnImage(int &outX, int &outY, int imageW, int imageH) {
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

// ─── Main ─────────────────────────────────────────────────────────

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <video_path>\n";
        return -1;
    }

    std::string videoPath = argv[1];

    cv::VideoCapture cap(videoPath);
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open video: " << videoPath << "\n";
        return -1;
    }

    double fps = cap.get(cv::CAP_PROP_FPS);
    int totalFrames = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_COUNT));
    std::cout << "Video: " << videoPath << "\n";
    std::cout << "FPS: " << fps << ", Total frames: " << totalFrames << "\n";

    cv::Mat frame;
    if (!cap.read(frame)) {
        std::cerr << "Error: Could not read first frame\n";
        return -1;
    }

    // ── Init GLFW + ImGui ──────────────────────────────────────────

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) {
        std::cerr << "Error: Could not initialize GLFW\n";
        return -1;
    }

    int winW = std::min(frame.cols + 100, 1600);
    int winH = std::min(frame.rows + 200, 1000);
    GLFWwindow *window = glfwCreateWindow(winW, winH, "Gauge Reader", nullptr, nullptr);
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

    // ── App State ──────────────────────────────────────────────────

    AppState state = STATE_INIT;
    GaugeDetector detector;
    VideoTexture videoTex;
    cv::Mat calibFrame;

    // Processing state
    std::deque<double> valueHistory;
    const int smoothWindow = 5;
    int frameCount = 0;
    double lastSmoothValue = 0;

    // Video writer
    cv::VideoWriter writer;

    // ── Main Loop ──────────────────────────────────────────────────

    while (!glfwWindowShouldClose(window)) {
        // ── Frame acquisition ────────────────────────────────────────
        if (state == STATE_PROCESSING) {
            if (!cap.read(frame)) break;
        }

        // ── State machine work (runs once per state entry) ───────────
        if (state == STATE_INIT && gCircleStage == 0) {
            std::cout << "Detecting gauge circle...\n";
            if (detector.detectCircle(frame)) {
                const auto &g = detector.gauge();
                std::cout << "  >> Gauge at (" << g.center.x << ", "
                          << g.center.y << "), radius=" << g.radius << "\n";
                calibFrame = frame.clone();
                cv::circle(calibFrame, g.center, g.radius, cv::Scalar(0, 255, 0), 2);
                state = STATE_CALIB_MIN;
            } else {
                gCircleStage = 1;
                state = STATE_CIRCLE_MANUAL;
                std::cout << "  >> Auto circle detection failed.\n";
                std::cout << "  >> Click center, then edge.\n";
            }
        }

        // ── Calibrate from manual circle clicks ──────────────────────
        if (state == STATE_CIRCLE_MANUAL && gCircleStage == 3 && gCircleRadius > 0) {
            detector.setCircle(gCircleCenter, gCircleRadius);
            const auto &g = detector.gauge();
            std::cout << "  >> Gauge at (" << g.center.x << ", "
                      << g.center.y << "), radius=" << g.radius << "\n";
            calibFrame = frame.clone();
            cv::circle(calibFrame, g.center, g.radius, cv::Scalar(0, 255, 0), 2);
            state = STATE_CALIB_MIN;
        }

        // ── Processing work ──────────────────────────────────────────
        if (state == STATE_PROCESSING) {
            double needleAngle = detector.detectNeedle(frame);
            double value = detector.angleToValue(needleAngle);

            if (value >= 0) {
                valueHistory.push_back(value);
                if (valueHistory.size() > smoothWindow)
                    valueHistory.pop_front();
            }

            lastSmoothValue = value;
            if (!valueHistory.empty()) {
                lastSmoothValue = std::accumulate(valueHistory.begin(), valueHistory.end(), 0.0)
                                  / valueHistory.size();
            }

            detector.drawOverlay(frame, needleAngle, lastSmoothValue);

            if (writer.isOpened())
                writer.write(frame);
        }

        // ── Build display image ──────────────────────────────────────
        cv::Mat disp;
        if (state == STATE_INIT) {
            disp = frame.clone();
        } else if (state == STATE_CIRCLE_MANUAL) {
            disp = frame.clone();
            if (gCircleStage == 2) {
                cv::circle(disp, gCircleCenter, 5, cv::Scalar(0, 255, 0), -1);
                cv::circle(disp, gCircleCenter, 30, cv::Scalar(0, 255, 0), 1);
            }
        } else if (state >= STATE_CALIB_MIN && state <= STATE_CALIB_CONFIRM) {
            disp = calibFrame.clone();
            if (state >= STATE_CALIB_MAX) {
                cv::circle(disp, gPtMin, 10, cv::Scalar(0, 255, 255), 2);
            }
            if (state == STATE_CALIB_CONFIRM) {
                cv::circle(disp, gPtMin, 10, cv::Scalar(0, 255, 0), 2);
                cv::circle(disp, gPtMax, 10, cv::Scalar(0, 0, 255), 2);
            }
        } else {
            disp = frame; // PROCESSING: overlay already drawn
        }

        // ── Upload texture ───────────────────────────────────────────
        videoTex.update(disp);

        // ── ImGui Frame ──────────────────────────────────────────────
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("Gauge Reader", nullptr,
                     ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoResize |
                     ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

        // ── Image ────────────────────────────────────────────────────
        float availW = ImGui::GetContentRegionAvail().x;
        float availH = ImGui::GetContentRegionAvail().y - 70;
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
        if (tryGetClickOnImage(clickX, clickY, disp.cols, disp.rows)) {
            if (state == STATE_CIRCLE_MANUAL) {
                if (gCircleStage == 1) {
                    gCircleCenter = cv::Point(clickX, clickY);
                    std::cout << "  >> Center at (" << clickX << ", " << clickY << ")\n";
                    gCircleStage = 2;
                } else if (gCircleStage == 2) {
                    gCircleRadius = cvRound(cv::norm(cv::Point(clickX, clickY) - gCircleCenter));
                    std::cout << "  >> Radius set to " << gCircleRadius << "\n";
                    gCircleStage = 3;
                }
            } else if (state == STATE_CALIB_MIN) {
                gPtMin = cv::Point(clickX, clickY);
                std::cout << "  >> Min marking at (" << clickX << ", " << clickY << ")\n";
                state = STATE_CALIB_MAX;
            } else if (state == STATE_CALIB_MAX) {
                gPtMax = cv::Point(clickX, clickY);
                std::cout << "  >> Max marking at (" << clickX << ", " << clickY << ")\n";
                state = STATE_CALIB_CONFIRM;
            }
        }

        // ── State UI ─────────────────────────────────────────────────
        ImGui::Spacing();

        if (state == STATE_INIT) {
            ImGui::Text("Detecting gauge circle...");
            ImGui::TextColored(ImVec4(1,1,0,1), "Press SPACE to retry");
        }

        if (state == STATE_CIRCLE_MANUAL) {
            if (gCircleStage == 1)
                ImGui::TextColored(ImVec4(1,1,0,1), "Click on the CENTER of the gauge");
            else
                ImGui::TextColored(ImVec4(1,1,0,1), "Now click on the EDGE of the gauge face");
        }

        if (state == STATE_CALIB_MIN)
            ImGui::TextColored(ImVec4(1,1,0,1), "Click on the MINIMUM value marking");

        if (state == STATE_CALIB_MAX)
            ImGui::TextColored(ImVec4(1,1,0,1), "Now click on the MAXIMUM value marking");

        if (state == STATE_CALIB_CONFIRM) {
            ImGui::SliderInt("Min value", &gCalibTrackMin, 0, 1000);
            ImGui::SliderInt("Max value", &gCalibTrackMax, 0, 1000);
            ImGui::Text("Min = %d   Max = %d", gCalibTrackMin, gCalibTrackMax);
            ImGui::Spacing();

            {
                float sa = detector.scale().startAngle;
                float ea = detector.scale().endAngle;
                if (ImGui::InputFloat("Min angle", &sa, 0.01f, 0.1f, "%.3f rad"))
                    detector.setStartAngle(sa);
                if (ImGui::InputFloat("Max angle", &ea, 0.01f, 0.1f, "%.3f rad"))
                    detector.setEndAngle(ea);
            }

            if (ImGui::Button("Confirm", ImVec2(120, 0))) {
                detector.calibrateFromPoints(gPtMin, gPtMax);
                detector.setCalibrationValues(gCalibTrackMin, gCalibTrackMax);
                detector.setCalibrationValid(true);

                const auto &s = detector.scale();
                std::cout << "  >> Scale: " << s.minValue << " at "
                          << (s.startAngle * 180.0 / PI) << " deg, "
                          << s.maxValue << " at "
                          << (s.endAngle * 180.0 / PI) << " deg\n";

                std::string outputPath = videoPath.substr(0, videoPath.find_last_of('.')) + "_output.avi";
                int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
                writer.open(outputPath, fourcc, fps, frame.size());
                if (writer.isOpened())
                    std::cout << "  >> Output: " << outputPath << "\n";

                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                state = STATE_PROCESSING;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) break;
        }

        if (state == STATE_PROCESSING) {
            ImGui::Text("Frame %d / %d", frameCount, totalFrames);
            ImGui::TextColored(ImVec4(0,1,1,1),
                               "Value: %.2f", lastSmoothValue);
            ImGui::Spacing();
            if (ImGui::Button("Restart", ImVec2(80, 0))) {
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                valueHistory.clear();
                frameCount = 0;
            }
            ImGui::SameLine();
            if (ImGui::Button("Quit", ImVec2(80, 0))) break;
            frameCount++;
        }

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

    if (frameCount > 0) {
        std::cout << "\n\nDone! " << frameCount << " frames processed.\n";
        if (!valueHistory.empty()) {
            double finalValue = std::accumulate(valueHistory.begin(), valueHistory.end(), 0.0)
                                / valueHistory.size();
            std::cout << "Final reading: " << std::fixed << std::setprecision(3)
                      << finalValue << "\n";
        }
    }

    videoTex.destroy();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    cap.release();
    if (writer.isOpened()) writer.release();

    return 0;
}
