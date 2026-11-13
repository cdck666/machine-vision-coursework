#include <algorithm>
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

std::vector<cv::Vec3f> detectHoles(const cv::Mat& image) {
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 1.1);

    // The small bores have a much smaller radius than the hub opening.
    const int side = std::min(gray.rows, gray.cols);
    std::vector<cv::Vec3f> candidates;
    cv::HoughCircles(gray, candidates, cv::HOUGH_GRADIENT, 1.0,
                     side * 0.085, 80, 16,
                     std::max(2, cvRound(side * 0.028)),
                     std::max(3, cvRound(side * 0.085)));

    std::vector<cv::Vec3f> holes;
    for (const auto& candidate : candidates) {
        const float x = candidate[0];
        const float y = candidate[1];
        const float radius = candidate[2];
        const float outerRadius = radius * 1.25f;
        if (x - outerRadius < 0 || x + outerRadius > gray.cols - 1 ||
            y - outerRadius < 0 || y + outerRadius > gray.rows - 1) continue;

        // A hole has a dark interior and a brighter rim in most directions.
        constexpr int samples = 32;
        int supported = 0;
        for (int i = 0; i < samples; ++i) {
            const double angle = 2 * CV_PI * i / samples;
            const double dx = std::cos(angle);
            const double dy = std::sin(angle);
            const auto inner = gray.at<unsigned char>(
                cvRound(y + radius * 0.72 * dy), cvRound(x + radius * 0.72 * dx));
            const auto outer = gray.at<unsigned char>(
                cvRound(y + outerRadius * dy), cvRound(x + outerRadius * dx));
            if (outer - inner >= 24) ++supported;
        }
        if (supported >= 26) holes.push_back(candidate);
    }
    return holes;
}

int main(int argc, char* argv[]) {
    try {
        fs::path input = "assets/holes.png";
        bool showWindow = true;
        bool hasInput = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--no-gui") {
                showWindow = false;
            } else if (!hasInput && arg.rfind("--", 0) != 0) {
                input = arg;
                hasInput = true;
            } else {
                std::cerr << "Usage: circular_hole_detection [image] [--no-gui]\n";
                return 2;
            }
        }

        const auto image = loadImage(input);
        const auto circles = detectHoles(image);
        cv::Mat result = image.clone();
        for (const auto& circle : circles) {
            const cv::Point center(cvRound(circle[0]), cvRound(circle[1]));
            cv::circle(result, center, cvRound(circle[2]), cv::Scalar(0, 0, 255),
                       2, cv::LINE_AA);
        }
        // Keep the count clear of the detected holes.
        cv::copyMakeBorder(result, result, 44, 0, 0, 0, cv::BORDER_CONSTANT,
                           cv::Scalar(245, 245, 245));
        const auto label = "Count: " + std::to_string(circles.size());
        cv::putText(result, label, cv::Point(12, 30), cv::FONT_HERSHEY_SIMPLEX,
                    0.85, cv::Scalar(0, 0, 210), 2, cv::LINE_AA);

        fs::create_directories("results");
        std::vector<unsigned char> encoded;
        if (!cv::imencode(".png", result, encoded)) {
            throw std::runtime_error("Cannot encode result image.");
        }
        std::ofstream output("results/holes_detected.png", std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        output.close();
        if (!output) throw std::runtime_error("Cannot save result image.");

        std::cout << label << "\nSaved: results/holes_detected.png\n";
        if (showWindow) {
            cv::imshow("Circular Hole Detection", result);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}


