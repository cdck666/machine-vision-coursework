#include <algorithm>
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

std::vector<cv::Vec3f> detectCoins(const cv::Mat& image) {
    cv::Mat gray;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 1.5);

    // Estimate the expected radius range from the scale of the sample.
    const int side = std::min(gray.rows, gray.cols);
    std::vector<cv::Vec3f> circles;
    cv::HoughCircles(gray, circles, cv::HOUGH_GRADIENT, 1.0,
                     side * 0.15, 100, 24,
                     std::max(1, cvRound(side * 0.07)),
                     std::max(2, cvRound(side * 0.15)));
    return circles;
}
int main(int argc, char* argv[]) {
    try {
        fs::path input = "assets/coins.png";
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
                std::cerr << "Usage: coin_counting [image] [--no-gui]\n";
                return 2;
            }
        }

        const auto image = loadImage(input);
        const auto circles = detectCoins(image);
        cv::Mat result = image.clone();
        for (const auto& circle : circles) {
            const cv::Point center(cvRound(circle[0]), cvRound(circle[1]));
            cv::circle(result, center, cvRound(circle[2]), cv::Scalar(0, 0, 255),
                       2, cv::LINE_AA);
        }
        // Keep the count clear of the detected coins.
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
        std::ofstream output("results/coins_counted.png", std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        output.close();
        if (!output) throw std::runtime_error("Cannot save result image.");

        std::cout << label << "\nSaved: results/coins_counted.png\n";
        if (showWindow) {
            cv::imshow("Coin Counting", result);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

