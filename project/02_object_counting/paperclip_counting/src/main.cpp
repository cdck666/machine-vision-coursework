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

std::vector<cv::RotatedRect> detectPaperclips(const cv::Mat& image) {
    cv::Mat gray, mask;
    cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0);
    cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY_INV | cv::THRESH_OTSU);
    const auto kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, kernel);

    // External contours count each clip once, even when it has an internal hole.
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    const double minArea = image.total() * 0.0005;
    std::vector<cv::RotatedRect> boxes;
    for (const auto& contour : contours) {
        if (cv::contourArea(contour) < minArea) continue;
        const auto box = cv::boundingRect(contour);
        // Ignore components touching the frame (e.g. the scan border).
        if (box.x == 0 || box.y == 0 || box.br().x == image.cols ||
            box.br().y == image.rows) continue;
        auto rotated = cv::minAreaRect(contour);
        rotated.size.width += 4;
        rotated.size.height += 4;
        boxes.push_back(rotated);
    }
    return boxes;
}

int main(int argc, char* argv[]) {
    try {
        fs::path input = "assets/paperclips.png";
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
                std::cerr << "Usage: paperclip_counting [image] [--no-gui]\n";
                return 2;
            }
        }

        const auto image = loadImage(input);
        const auto boxes = detectPaperclips(image);
        cv::Mat result = image.clone();
        for (const auto& box : boxes) {
            cv::Point2f vertices[4];
            box.points(vertices);
            for (int i = 0; i < 4; ++i) {
                cv::line(result, vertices[i], vertices[(i + 1) % 4],
                         cv::Scalar(0, 0, 255), 2, cv::LINE_AA);
            }
        }
        // Add a title strip so the count does not cover any detected clip.
        cv::copyMakeBorder(result, result, 44, 0, 0, 0, cv::BORDER_CONSTANT,
                           cv::Scalar(245, 245, 245));
        const auto label = "Count: " + std::to_string(boxes.size());
        cv::putText(result, label, cv::Point(12, 30), cv::FONT_HERSHEY_SIMPLEX,
                    0.85, cv::Scalar(0, 0, 210), 2, cv::LINE_AA);

        fs::create_directories("results");
        std::vector<unsigned char> encoded;
        if (!cv::imencode(".png", result, encoded)) {
            throw std::runtime_error("Cannot encode result image.");
        }
        std::ofstream output("results/paperclips_counted.png", std::ios::binary);
        output.write(reinterpret_cast<const char*>(encoded.data()),
                     static_cast<std::streamsize>(encoded.size()));
        output.close();
        if (!output) throw std::runtime_error("Cannot save result image.");

        std::cout << label << "\nSaved: results/paperclips_counted.png\n";
        if (showWindow) {
            cv::imshow("Paperclip Counting", result);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
