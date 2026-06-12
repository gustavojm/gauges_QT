#include "gauge_detector.h"

#include <opencv2/opencv.hpp>
#include <GLFW/glfw3.h>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <iostream>
#include <cmath>
#include <string>
#include <algorithm>
#include <iomanip>
#include <numeric>
#include <deque>

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

    std::vector<GaugeDetector> detectors;
    size_t currentGaugeIdx = 0;
    VideoTexture videoTex;
    cv::Mat calibFrame;
    int frameCount = 0;

    // Video writer
    cv::VideoWriter writer;

    // ── Initialization: find all gauges ────────────────────────────

    std::cout << "Detecting gauge circles...\n";
    std::vector<GaugeROI> gauges = GaugeDetector::findGauges(frame);
    if (gauges.empty()) {
        std::cout << "  >> No gauges found. Switching to manual mode.\n";
        std::cout << "  >> Click center, then edge.\n";
        detectors.emplace_back();
        detectors[0].setState(GaugeState::CIRCLE_MANUAL);
        detectors[0].setCircleStage(1);
    } else {
        std::cout << "  >> Found " << gauges.size() << " gauge(s):\n";
        for (size_t i = 0; i < gauges.size(); i++) {
            GaugeDetector d;
            d.setCircle(gauges[i].center, gauges[i].radius);
            std::cout << "    [" << i << "] at (" << gauges[i].center.x << ", "
                      << gauges[i].center.y << "), radius=" << gauges[i].radius << "\n";
            detectors.push_back(std::move(d));
        }
        calibFrame = frame.clone();
        for (const auto &d : detectors) {
            const auto &g = d.gauge();
            cv::circle(calibFrame, g.center, g.radius, cv::Scalar(0, 255, 0), 2);
        }
        detectors[currentGaugeIdx].setState(GaugeState::CALIB_MIN);
    }

    // ── Main Loop ──────────────────────────────────────────────────

    while (!glfwWindowShouldClose(window)) {
        bool allProcessing = !detectors.empty();
        for (const auto &d : detectors)
            if (d.state() != GaugeState::PROCESSING) { allProcessing = false; break; }

        // ── Frame acquisition (processing mode only) ────────────────
        if (allProcessing) {
            if (!cap.read(frame)) break;
        }

        // ── State machine transitions ───────────────────────────────
        // if (!allProcessing && !detectors.empty()) {
        //     // Manual circle -> calibration transition
        //     if (gauges.empty()) {
        //         auto &d = detectors[0];
        //         if (d.state() == GaugeState::CIRCLE_MANUAL &&
        //             d.circleStage() == 3 && d.circleRadius() > 0) {
        //             d.setCircle(d.circleCenter(), d.circleRadius());
        //             const auto &g = d.gauge();
        //             std::cout << "  >> Gauge at (" << g.center.x << ", "
        //                       << g.center.y << "), radius=" << g.radius << "\n";
        //             calibFrame = frame.clone();
        //             cv::circle(calibFrame, g.center, g.radius,
        //                        cv::Scalar(0, 255, 0), 2);
        //             d.setState(GaugeState::CALIB_MIN);
        //         }
        //     }
        // }

        // ── Processing work ──────────────────────────────────────────
        if (allProcessing) {
            for (auto &d : detectors)
                d.detectNeedle(frame);

            int labelY = 60;
            for (auto &d : detectors) {
                d.drawOverlay(frame, labelY);
                labelY += 30;
            }

            if (writer.isOpened())
                writer.write(frame);
        }

        // ── Build display image ──────────────────────────────────────
        cv::Mat disp;
        if (allProcessing) {
            disp = frame;
        } else if (!calibFrame.empty()) {
            disp = calibFrame.clone();
            if (!detectors.empty()) {
                size_t idx = gauges.empty() ? 0 : currentGaugeIdx;
                auto &d = detectors[idx];
                if (d.state() == GaugeState::CALIB_MAX) {
                    cv::circle(disp, d.ptMin(), 10, cv::Scalar(0, 255, 255), 2);
                } else if (d.state() == GaugeState::CALIB_CONFIRM) {
                    cv::circle(disp, d.ptMin(), 10, cv::Scalar(0, 255, 0), 2);
                    cv::circle(disp, d.ptMax(), 10, cv::Scalar(0, 0, 255), 2);
                }
            }
        } else {
            disp = frame.clone();
            if (!detectors.empty()) {
                auto &d = detectors[0];
                if (d.state() == GaugeState::CIRCLE_MANUAL &&
                    d.circleStage() == 2) {
                    cv::circle(disp, d.circleCenter(), 5, cv::Scalar(0, 255, 0), -1);
                    cv::circle(disp, d.circleCenter(), 30, cv::Scalar(0, 255, 0), 1);
                }
            }
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
            if (!detectors.empty()) {
                size_t idx = gauges.empty() ? 0 : currentGaugeIdx;
                auto &d = detectors[idx];
                if (d.state() == GaugeState::CIRCLE_MANUAL) {
                    if (d.circleStage() == 1) {
                        d.setCircleCenter(cv::Point(clickX, clickY));
                        std::cout << "  >> Center at (" << clickX << ", "
                                  << clickY << ")\n";
                        d.setCircleStage(2);
                    } else if (d.circleStage() == 2) {
                        int r = cvRound(cv::norm(cv::Point(clickX, clickY) -
                                                  d.circleCenter()));
                        d.setCircleRadius(r);
                        std::cout << "  >> Radius set to " << r << "\n";
                        d.setCircleStage(3);
                    }
                } else if (d.state() == GaugeState::CALIB_MIN) {
                    d.setPtMin(cv::Point(clickX, clickY));
                    std::cout << "  >> Min marking at (" << clickX << ", "
                              << clickY << ")\n";
                    d.setState(GaugeState::CALIB_MAX);
                } else if (d.state() == GaugeState::CALIB_MAX) {
                    d.setPtMax(cv::Point(clickX, clickY));
                    std::cout << "  >> Max marking at (" << clickX << ", "
                              << clickY << ")\n";
                    d.setState(GaugeState::CALIB_CONFIRM);
                }
            }
        }

        // ── State UI ─────────────────────────────────────────────────
        ImGui::Spacing();

        if (!allProcessing && !detectors.empty()) {
            size_t idx = gauges.empty() ? 0 : currentGaugeIdx;
            auto &d = detectors[idx];

            if (d.state() == GaugeState::CIRCLE_MANUAL) {
                if (d.circleStage() == 1)
                    ImGui::TextColored(ImVec4(1,1,0,1),
                                       "Click on the CENTER of the gauge");
                else
                    ImGui::TextColored(ImVec4(1,1,0,1),
                                       "Now click on the EDGE of the gauge face");
            }

            if (d.state() == GaugeState::CALIB_MIN) {
                if (gauges.size() > 1)
                    ImGui::Text("Gauge %zu / %zu",
                                currentGaugeIdx + 1, detectors.size());
                ImGui::TextColored(ImVec4(1,1,0,1),
                                   "Click on the MINIMUM value marking");
            }

            if (d.state() == GaugeState::CALIB_MAX)
                ImGui::TextColored(ImVec4(1,1,0,1),
                                   "Now click on the MAXIMUM value marking");

            if (d.state() == GaugeState::CALIB_CONFIRM) {
                if (gauges.size() > 1)
                    ImGui::Text("Gauge %zu / %zu",
                                currentGaugeIdx + 1, detectors.size());

                int minVal = d.calibTrackMin();
                int maxVal = d.calibTrackMax();
                if (ImGui::SliderInt("Min value", &minVal, 0, 1000))
                    d.setCalibTrackMin(minVal);
                if (ImGui::SliderInt("Max value", &maxVal, 0, 1000))
                    d.setCalibTrackMax(maxVal);
                ImGui::Text("Min = %d   Max = %d",
                            d.calibTrackMin(), d.calibTrackMax());
                ImGui::Spacing();

                {
                    float sa = static_cast<float>(d.scale().startAngle);
                    float ea = static_cast<float>(d.scale().endAngle);
                    if (ImGui::InputFloat("Min angle", &sa, 0.01f, 0.1f, "%.3f rad"))
                        d.setStartAngle(sa);
                    if (ImGui::InputFloat("Max angle", &ea, 0.01f, 0.1f, "%.3f rad"))
                        d.setEndAngle(ea);
                }

                if (ImGui::Button("Confirm", ImVec2(120, 0))) {
                    d.calibrateFromPoints(d.ptMin(), d.ptMax());
                    d.setCalibrationValues(d.calibTrackMin(), d.calibTrackMax());
                    d.setCalibrationValid(true);

                    const auto &s = d.scale();
                    std::cout << "  >> Gauge " << idx << " scale: "
                              << s.minValue << " at "
                              << (s.startAngle * 180.0 / PI) << " deg, "
                              << s.maxValue << " at "
                              << (s.endAngle * 180.0 / PI) << " deg\n";

                    if (currentGaugeIdx + 1 < detectors.size()) {
                        currentGaugeIdx++;
                        detectors[currentGaugeIdx].setState(GaugeState::CALIB_MIN);
                    } else {
                        std::string outputPath =
                            videoPath.substr(0, videoPath.find_last_of('.'))
                            + "_output.avi";
                        int fourcc = cv::VideoWriter::fourcc('M', 'J', 'P', 'G');
                        writer.open(outputPath, fourcc, fps, frame.size());
                        if (writer.isOpened())
                            std::cout << "  >> Output: " << outputPath << "\n";
                        cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                        for (auto &det : detectors)
                            det.setState(GaugeState::PROCESSING);
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0))) break;
            }
        }

        if (allProcessing) {
            ImGui::Text("Frame %d / %d", frameCount, totalFrames);
            for (size_t i = 0; i < detectors.size(); i++) {
                ImGui::TextColored(ImVec4(0,1,1,1),
                                   "Gauge %zu: %.2f",
                                   i + 1, detectors[i].getSmoothedValue());
            }
            ImGui::Spacing();
            if (ImGui::Button("Restart", ImVec2(80, 0))) {
                cap.set(cv::CAP_PROP_POS_FRAMES, 0);
                for (auto &d : detectors) d.resetSmoothing();
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
