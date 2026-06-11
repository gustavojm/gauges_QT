/**
 * @file HoughCircle_Demo.cpp
 * @brief Demo code for Hough Transform
 * @author OpenCV team
 */

#include "opencv2/opencv.hpp"
#include "opencv2/highgui.hpp"
#include "opencv2/imgproc.hpp"
#include <iostream>
#include <optional>

struct my_circle {
    cv::Point center;
    int radius;
};

std::optional<my_circle> selected;

namespace {
// windows and trackbars name
const std::string window_name = "Hough Circle Detection Demo";
const std::string canny_threshold_trackbar_nme = "Canny threshold";
const std::string accumulator_threshold_trackbar_name = "Accumulator Threshold";


// initial and max values of the parameters of interests.
const int canny_threshold_initial_value = 100;
const int accumulator_threshold_initialValue = 50;
const int max_accumulator_threshold = 200;
const int max_canny_threshold = 255;

std::vector<my_circle> HoughDetection(const cv::Mat &src_gray, const cv::Mat &src_display,
        int cannyThreshold, int accumulatorThreshold, std::optional<my_circle> selected) {
    // will hold the results of the detection
    std::vector<cv::Vec3f> circles;
    std::vector<my_circle> my_circles;
    // runs the actual detection
    HoughCircles(src_gray, circles, cv::HOUGH_GRADIENT, 1, src_gray.rows / 8,
            cannyThreshold, accumulatorThreshold, 0, 0);

    // clone the colour, input image for displaying purposes
    cv::Mat display = src_display.clone();
    for (size_t i = 0; i < circles.size(); i++) {
        cv::Point center(cvRound(circles[i][0]), cvRound(circles[i][1]));
        int radius = cvRound(circles[i][2]);
        my_circles.push_back(my_circle {center, radius});

        // circle center
        circle(display, center, 3, cv::Scalar(0, 255, 0), -1, 8, 0);
        // circle outline
        circle(display, center, radius, cv::Scalar(0, 0, 255), 3, 8, 0);
    }

    if (selected) {
        // circle outline
        cv::circle(display, (*selected).center, (*selected).radius, cv::Scalar(255, 255, 255), 3, 8, 0);
    }

    // shows the results
    cv::imshow(window_name, display);
    return my_circles;
}
}

void CallBackFunc(int event, int x, int y, int flags, void *userdata) {
    if (event == cv::EVENT_LBUTTONDOWN) {
        std::cout << "Left button of the mouse is clicked - position (" << x << ", "
                << y << ")" << std::endl;

        if (userdata != nullptr) {
            std::vector<my_circle> *circles = reinterpret_cast<std::vector<my_circle> *>(userdata);
            for (const auto &circle : *circles) {
                std::cout << circle.center << std::endl;
                float dist = norm(circle.center - cv::Point(x, y));
                if (static_cast<int>(dist) < circle.radius) {
                    if (selected) {
                        if (selected->center == circle.center) {
                            selected = {};
                        } else {
                            selected = circle;
                        }
                    } else {
                        selected = circle;
                    }
                }
            }
        }

    } else if (event == cv::EVENT_RBUTTONDOWN) {
        std::cout << "Right button of the mouse is clicked - position (" << x << ", "
                << y << ")" << std::endl;
    } else if (event == cv::EVENT_MBUTTONDOWN) {
        std::cout << "Middle button of the mouse is clicked - position (" << x
                << ", " << y << ")" << std::endl;
    } else if (event == cv::EVENT_MOUSEMOVE) {
        //std::cout << "Mouse move over the window - position (" << x << ", " << y
        //        << ")" << std::endl;

    }
}

int main(int argc, char **argv) {
    cv::Mat src, src_gray;
    std::vector<cv::Vec3f> circles;
    std::vector<my_circle> my_circles;

    // Read the image
    std::string imageName("../resources/circles.png");  // by default
    if (argc > 1) {
        imageName = argv[1];
    }
    src = cv::imread(cv::samples::findFile(imageName), cv::IMREAD_COLOR);

    if (src.empty()) {
        std::cerr << "Invalid input image\n";
        std::cout << "Usage : " << argv[0] << " <path_to_input_image>\n";
        ;
        return -1;
    }

    // Convert it to gray
    cv::cvtColor(src, src_gray, cv::COLOR_BGR2GRAY);

    // Reduce the noise so we avoid false circle detection
    cv::GaussianBlur(src_gray, src_gray, cv::Size(9, 9), 2, 2);

    //declare and initialize both parameters that are subjects to change
    int cannyThreshold = canny_threshold_initial_value;
    int accumulatorThreshold = accumulator_threshold_initialValue;

    // create the main window, and attach the trackbars
    cv::namedWindow(window_name, cv::WINDOW_AUTOSIZE);
    cv::createTrackbar(canny_threshold_trackbar_nme, window_name, &cannyThreshold,
            max_canny_threshold);
    cv::createTrackbar(accumulator_threshold_trackbar_name, window_name,
            &accumulatorThreshold, max_accumulator_threshold);

    //set the callback function for any mouse event
    cv::setMouseCallback(window_name, CallBackFunc, &my_circles);

    // infinite loop to display
    // and refresh the content of the output image
    // until the user presses q or Q
    char key = 0;
    while (key != 'q' && key != 'Q') {
        // those parameters cannot be =0
        // so we must check here
        cannyThreshold = std::max(cannyThreshold, 1);
        accumulatorThreshold = std::max(accumulatorThreshold, 1);

        //runs the detection, and update the display
        my_circles = HoughDetection(src_gray, src, cannyThreshold, accumulatorThreshold, selected);

        // get user key
        key = (char) cv::waitKey(10);
    }

    return 0;
}
