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

struct Edge {
    cv::Point first;
    cv::Point second;
    double length;
};

std::vector<Edge> findLongestEdges(const cv::Mat& gray) {
    // The bright workpiece is the largest foreground component in this scene.
    cv::Mat mask;
    cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    if (contours.empty()) throw std::runtime_error("Workpiece contour not found.");
    const auto outer = std::max_element(contours.begin(), contours.end(),
        [](const auto& a, const auto& b) { return cv::contourArea(a) < cv::contourArea(b); });

    // Simplify the outer contour into straight segments before ranking them.
    std::vector<cv::Point> polygon;
    cv::approxPolyDP(*outer, polygon, 0.012 * cv::arcLength(*outer, true), true);
    if (polygon.size() < 3) throw std::runtime_error("Workpiece contour is too small.");
    std::vector<Edge> edges;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i];
        const auto b = polygon[(i + 1) % polygon.size()];
        edges.push_back({a, b, cv::norm(a - b)});
    }
    std::sort(edges.begin(), edges.end(),
        [](const Edge& a, const Edge& b) { return a.length > b.length; });
    edges.resize(2);
    return edges;
}

// Draw the supporting line, extended to the image borders like the example.
void markEdge(cv::Mat& image, const Edge& edge) {
    const cv::Point delta = edge.second - edge.first;
    const double scale = 2.0 * std::max(image.cols, image.rows) / edge.length;
    const cv::Point offset(cvRound(delta.x * scale), cvRound(delta.y * scale));
    cv::Point first = edge.first - offset;
    cv::Point second = edge.second + offset;
    if (cv::clipLine(image.size(), first, second)) {
        cv::line(image, first, second, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
    }
}

int main(int argc, char* argv[]) {
    const bool noGui = argc == 2 && std::string(argv[1]) == "--no-gui";
    if (argc != 1 && !noGui) {
        std::cerr << "Usage: edge_line_detection [--no-gui]\n";
        return 2;
    }
    try {
        const cv::Mat image = loadImage("assets/workpiece.png");
        cv::Mat gray, edges;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0);
        cv::Canny(gray, edges, 35, 90, 3, true);

        const auto longest = findLongestEdges(gray);
        cv::Mat marked = image.clone();
        for (const auto& edge : longest) {
            markEdge(marked, edge);
            std::cout << edge.first << " -> " << edge.second
                      << ", " << edge.length << " px\n";
        }
        fs::create_directories("results");
        if (!cv::imwrite("results/canny.png", edges) ||
            !cv::imwrite("results/long_edges.png", marked)) {
            throw std::runtime_error("Cannot save results.");
        }
        if (!noGui) {
            // Enlarge the previews without changing detection coordinates.
            cv::Mat edgesPreview, markedPreview;
            cv::resize(edges, edgesPreview, {}, 4, 4, cv::INTER_NEAREST);
            cv::resize(marked, markedPreview, {}, 4, 4, cv::INTER_NEAREST);
            cv::imshow("Canny", edgesPreview);
            cv::imshow("Two Longest Edges", markedPreview);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
