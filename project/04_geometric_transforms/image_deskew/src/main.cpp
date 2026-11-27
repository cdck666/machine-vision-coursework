#include <algorithm>
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

cv::Mat deskew(const cv::Mat& image, double& angle) {
    constexpr int border = 4;
    if (image.cols <= 2 * border || image.rows <= 2 * border) {
        throw std::runtime_error("Input image is too small.");
    }
    cv::Mat white, foreground;
    cv::inRange(image, cv::Scalar(245, 245, 245), cv::Scalar(255, 255, 255), white);
    // Exclude the thin black frame around the supplied image.
    foreground = cv::Mat::zeros(image.size(), CV_8U);
    const cv::Rect interior(border, border, image.cols - 2 * border, image.rows - 2 * border);
    cv::bitwise_not(white(interior), foreground(interior));

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(foreground, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) throw std::runtime_error("Image boundary not found.");
    const auto contour = std::max_element(contours.begin(), contours.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });
    const auto box = cv::minAreaRect(*contour);
    if (box.size.area() < 0.15 * image.total() ||
        cv::contourArea(*contour) < 0.85 * box.size.area()) {
        throw std::runtime_error("No large rectangular image boundary found.");
    }

    angle = box.angle;
    auto size = box.size;
    // Choose the orientation closest to horizontal.
    if (angle > 45) {
        angle -= 90;
        std::swap(size.width, size.height);
    } else if (angle < -45) {
        angle += 90;
        std::swap(size.width, size.height);
    }
    // Crop slightly inside the estimated boundary to avoid resampling the border.
    const cv::Size outputSize(cvFloor(size.width) - 2 * border,
                              cvFloor(size.height) - 2 * border);
    if (outputSize.width <= 0 || outputSize.height <= 0) {
        throw std::runtime_error("Detected image boundary is too small.");
    }
    auto transform = cv::getRotationMatrix2D(box.center, angle, 1.0);
    transform.at<double>(0, 2) += (outputSize.width - 1) * 0.5 - box.center.x;
    transform.at<double>(1, 2) += (outputSize.height - 1) * 0.5 - box.center.y;
    cv::Mat corrected;
    cv::warpAffine(image, corrected, transform, outputSize, cv::INTER_LINEAR);
    return corrected;
}

int main(int argc, char* argv[]) {
    const bool noGui = argc == 2 && std::string(argv[1]) == "--no-gui";
    if (argc != 1 && !noGui) {
        std::cerr << "Usage: image_deskew [--no-gui]\n";
        return 2;
    }
    try {
        const auto input = loadImage("assets/tilted.png");
        double angle = 0;
        const auto corrected = deskew(input, angle);
        fs::create_directories("results");
        if (!cv::imwrite("results/straightened.png", corrected)) {
            throw std::runtime_error("Cannot save result.");
        }
        std::cout << "Correction angle (CCW): " << angle << " deg\n"
                  << "Output: " << corrected.cols << " x " << corrected.rows
                  << "\nSaved: results/straightened.png\n";
        if (!noGui) {
            cv::imshow("Tilted Image", input);
            cv::imshow("Straightened Image", corrected);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
