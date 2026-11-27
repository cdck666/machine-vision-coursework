#include <algorithm>
#include <array>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

namespace fs = std::filesystem;

cv::Mat loadImage(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Cannot open input image.");
    const std::vector<unsigned char> bytes{
        std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    if (bytes.empty()) throw std::runtime_error("Input image is empty.");
    cv::Mat image = cv::imdecode(bytes, cv::IMREAD_COLOR);
    if (image.empty()) throw std::runtime_error("Cannot decode input image.");
    return image;
}

std::array<cv::Point2f, 4> findCoverCorners(const cv::Mat& image) {
    cv::Mat hsv, mask;
    cv::cvtColor(image, hsv, cv::COLOR_BGR2HSV);
    cv::inRange(hsv, cv::Scalar(85, 25, 35), cv::Scalar(135, 255, 255), mask);
    const int radius = std::max(1, cvRound(std::min(image.cols, image.rows) * 0.0075));
    const int size = 2 * radius + 1;
    const auto kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, {size, size});
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) throw std::runtime_error("No blue cover detected.");
    const auto cover = std::max_element(contours.begin(), contours.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
    std::vector<cv::Point> hull, quad;
    // The hull bridges gaps where light graphics reach the cover boundary.
    cv::convexHull(*cover, hull, false);
    cv::approxPolyDP(hull, quad, 0.02 * cv::arcLength(hull, true), true);
    if (quad.size() != 4 || cv::contourArea(quad) < 0.10 * image.total()) {
        throw std::runtime_error("Could not locate a large four-corner cover.");
    }

    // In this upright portrait view, the top edge has the highest midpoint.
    size_t top = 0;
    for (size_t i = 1; i < quad.size(); ++i) {
        if (quad[i].y + quad[(i + 1) % 4].y < quad[top].y + quad[(top + 1) % 4].y) {
            top = i;
        }
    }
    const size_t next = (top + 1) % 4;
    if (quad[top].x < quad[next].x) {
        return {cv::Point2f(quad[top]), cv::Point2f(quad[next]),
                cv::Point2f(quad[(top + 2) % 4]), cv::Point2f(quad[(top + 3) % 4])};
    }
    return {cv::Point2f(quad[next]), cv::Point2f(quad[top]),
            cv::Point2f(quad[(top + 3) % 4]), cv::Point2f(quad[(top + 2) % 4])};
}

cv::Mat rectifyCover(const cv::Mat& image, const std::array<cv::Point2f, 4>& corners) {
    const int width = cvRound(std::max(cv::norm(corners[1] - corners[0]),
                                       cv::norm(corners[2] - corners[3])));
    const int height = cvRound(std::max(cv::norm(corners[3] - corners[0]),
                                        cv::norm(corners[2] - corners[1])));
    constexpr int trim = 2;
    if (width <= 2 * trim || height <= 2 * trim) {
        throw std::runtime_error("Cover dimensions are invalid.");
    }
    const float right = static_cast<float>(width - 1);
    const float bottom = static_cast<float>(height - 1);
    const std::array<cv::Point2f, 4> target{{{0, 0}, {right, 0}, {right, bottom}, {0, bottom}}};
    const auto transform = cv::getPerspectiveTransform(corners.data(), target.data());
    cv::Mat corrected;
    cv::warpPerspective(image, corrected, transform, {width, height});
    // Trim the small mixed foreground/background strip at the sampled boundary.
    return corrected(cv::Rect(trim, trim, width - 2 * trim, height - 2 * trim)).clone();
}

int main(int argc, char* argv[]) {
    const bool noGui = argc == 2 && std::string(argv[1]) == "--no-gui";
    if (argc != 1 && !noGui) {
        std::cerr << "Usage: book_rectification [--no-gui]\n";
        return 2;
    }
    try {
        const auto input = loadImage("assets/book.png");
        const auto corners = findCoverCorners(input);
        const auto corrected = rectifyCover(input, corners);
        fs::create_directories("results");
        if (!cv::imwrite("results/rectified_book.png", corrected)) {
            throw std::runtime_error("Cannot save result.");
        }
        std::cout << "Corners (TL, TR, BR, BL):";
        for (const auto& corner : corners) std::cout << ' ' << corner;
        std::cout << "\nOutput: " << corrected.cols << " x " << corrected.rows
                  << "\nSaved: results/rectified_book.png\n";
        if (!noGui) {
            cv::imshow("Original Book Photo", input);
            cv::imshow("Rectified Book Cover", corrected);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
