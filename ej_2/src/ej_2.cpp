#include <opencv2/imgcodecs.hpp>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <iostream>
#include <algorithm>

#if 0
struct AllocationMetrics {
    uint32_t TotalAllocated = 0;
    uint32_t TotalFreed = 0;

    uint32_t CurrentUssage() {
        return TotalAllocated - TotalFreed;
    }
};

static AllocationMetrics s_AllocationMetrics;

void* operator new(size_t size) {                // Overloading the new operator
    std::cout << "Allocating " << size << " bytes\n";
    s_AllocationMetrics.TotalAllocated += size;
    return malloc(size);
}

void operator delete(void *memory, size_t size) {
    std::cout << "Freeing " << size << " bytes\n";
    s_AllocationMetrics.TotalFreed += size;
    free(memory);
}

static void PrintMemoryUsage() {
    std::cout << "Memory usage: " << s_AllocationMetrics.CurrentUssage()
            << " bytes \n";
}

#endif

///////////////  Project 2 - Document Scanner  //////////////////////

std::vector<cv::Point> initialPoints, docPoints;
float w = 420, h = 596;

cv::Mat preProcessing(const cv::Mat &img) {
    cv::Mat imgGray;
    cv::cvtColor(img, imgGray, cv::COLOR_BGR2GRAY);

    cv::Mat imgBlur;
    cv::GaussianBlur(imgGray, imgBlur, cv::Size(3, 3), 3, 0);

    cv::Mat imgCanny;
    cv::Canny(imgBlur, imgCanny, 25, 75);
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));

    cv::Mat imgDil;
    cv::dilate(imgCanny, imgDil, kernel);
    //cv::erode(imgDil, imgErode, kernel);
    return imgDil;
}

std::vector<cv::Point> getContours(const cv::Mat &image) {

    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;

    findContours(image, contours, hierarchy, cv::RETR_EXTERNAL,
            cv::CHAIN_APPROX_SIMPLE);
    //drawContours(img, contours, -1, Scalar(255, 0, 255), 2);
    std::vector<cv::Point> conPoly;
    cv::Rect boundRect;

    std::vector<cv::Point> biggest;
    int maxArea = 0;

    for (const auto &contour : contours) {
        int area = contourArea(contour);

        if (area > 1000) {
            float peri = cv::arcLength(contour, true);
            cv::approxPolyDP(contour, conPoly, 0.02 * peri, true);

            if (area > maxArea && conPoly.size() == 4) {

                //drawContours(imgOriginal, conPoly, i, Scalar(255, 0, 255), 5);
                biggest = conPoly;
                maxArea = area;
            }
            //drawContours(imgOriginal, conPoly, i, Scalar(255, 0, 255), 2);
            //rectangle(imgOriginal, boundRect[i].tl(), boundRect[i].br(), Scalar(0, 255, 0), 5);
        }
    }
    return biggest;
}

void drawPoints(const cv::Mat &imgOriginal,
        const std::vector<cv::Point> &points, const cv::Scalar &color) {
    for (int i = 0; i < static_cast<int>(points.size()); i++) {
        cv::circle(imgOriginal, points[i], 10, color, cv::FILLED);
        putText(imgOriginal, std::to_string(i), points[i],
                cv::FONT_HERSHEY_PLAIN, 4, color, 4);
    }
}

std::vector<cv::Point> reorder(const std::vector<cv::Point> &points) {
    std::vector<cv::Point> orderedPoints;
    std::vector<int> sumPoints, subPoints;

    for (const auto &point : points) {
        sumPoints.push_back(point.x + point.y);
        subPoints.push_back(point.x - point.y);
    }

//    std::transform(points.begin(), points.end(), points.begin(),
//            std::back_inserter(sumPoints), [](cv::Point i, cv::Point j) {
//                return i.x + j.y;
//            });
//
//    std::transform(points.begin(), points.end(), points.begin(),
//            std::back_inserter(subPoints), [](cv::Point i, cv::Point j) {
//                return i.x - j.y;
//            });

    orderedPoints.push_back(
            points[std::min_element(sumPoints.begin(), sumPoints.end())
                    - sumPoints.begin()]);  // 0
    orderedPoints.push_back(
            points[std::max_element(subPoints.begin(), subPoints.end())
                    - subPoints.begin()]);  // 1
    orderedPoints.push_back(
            points[std::min_element(subPoints.begin(), subPoints.end())
                    - subPoints.begin()]);  // 2
    orderedPoints.push_back(
            points[std::max_element(sumPoints.begin(), sumPoints.end())
                    - sumPoints.begin()]);  // 3

    return orderedPoints;
}

cv::Mat getWarp(const cv::Mat &img, std::vector<cv::Point> &points,
        const float w, const float h) {
    cv::Point2f src[4] = { points[0], points[1], points[2], points[3] };
    cv::Point2f dst[4] = { { 0.0f, 0.0f }, { w, 0.0f }, { 0.0f, h }, { w, h } };

    cv::Mat imgWarp;
    cv::Mat Matrix = getPerspectiveTransform(src, dst);
    warpPerspective(img, imgWarp, Matrix, cv::Point(w, h));

    return imgWarp;
}

int main() {

    std::string path = "../resources/paper.jpg";
    cv::Mat imgOriginal = cv::imread(path);
    //resize(imgOriginal, imgOriginal, Size(), 0.5, 0.5);

    // Preprocessing - Step 1
    cv::Mat imgThre = preProcessing(imgOriginal);

    // Get Contours - Biggest  - Step 2
    initialPoints = getContours(imgThre);
    drawPoints(imgOriginal, initialPoints, cv::Scalar(0, 0, 255));
    docPoints = reorder(initialPoints);
    //drawPoints(docPoints, Scalar(0, 255, 0));

    // Warp - Step 3
    cv::Mat imgWarp = getWarp(imgOriginal, docPoints, w, h);

    //Crop - Step 4
    int cropVal = 5;
    cv::Rect roi(cropVal, cropVal, w - (2 * cropVal), h - (2 * cropVal));
    cv::Mat imgCrop = imgWarp(roi);

    cv::imshow("Image", imgOriginal);
    //cv::imshow("Image Dilation", imgThre);
    //cv::imshow("Image Warp", imgWarp);
    cv::imshow("Image Crop", imgCrop);
    cv::waitKey(0);
    return 1;
}
