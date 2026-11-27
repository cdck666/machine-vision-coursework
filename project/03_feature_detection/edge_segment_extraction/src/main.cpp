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

std::vector<Edge> contourEdges(const std::vector<cv::Point>& contour, double tolerance) {
    std::vector<cv::Point> polygon;
    cv::approxPolyDP(contour, polygon, tolerance, true);
    std::vector<Edge> edges;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i];
        const auto b = polygon[(i + 1) % polygon.size()];
        edges.push_back({a, b, cv::norm(b - a)});
    }
    return edges;
}

void sortByLength(std::vector<Edge>& edges) {
    std::sort(edges.begin(), edges.end(),
        [](const Edge& a, const Edge& b) { return a.length > b.length; });
}

bool touches(const Edge& a, const Edge& b) {
    return a.first == b.first || a.first == b.second ||
           a.second == b.first || a.second == b.second;
}

std::vector<Edge> extractSegments(const cv::Mat& gray) {
    cv::Mat mask;
    cv::threshold(gray, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
    std::vector<std::vector<cv::Point>> contours;
    std::vector<cv::Vec4i> hierarchy;
    cv::findContours(mask, contours, hierarchy, cv::RETR_TREE, cv::CHAIN_APPROX_SIMPLE);
    int body = -1;
    int slot = -1;
    double bodyArea = 0;
    double slotArea = 0;
    for (int i = 0; i < static_cast<int>(contours.size()); ++i) {
        const double area = cv::contourArea(contours[i]);
        if (hierarchy[i][3] == -1 && area > bodyArea) {
            body = i;
            bodyArea = area;
        }
    }
    if (body < 0) throw std::runtime_error("Workpiece contour not found.");

    // The rectangular slot is the largest direct internal opening in this part.
    for (int i = hierarchy[body][2]; i >= 0; i = hierarchy[i][0]) {
        const double area = cv::contourArea(contours[i]);
        if (area > slotArea) {
            slot = i;
            slotArea = area;
        }
    }
    if (slot < 0) throw std::runtime_error("Slot contour not found.");

    auto outer = contourEdges(contours[body], 0.012 * cv::arcLength(contours[body], true));
    auto inner = contourEdges(contours[slot], 0.03 * cv::arcLength(contours[slot], true));
    if (outer.size() < 4 || inner.size() != 4) {
        throw std::runtime_error("Unexpected contour geometry.");
    }
    sortByLength(outer);
    sortByLength(inner);

    // The straight end joins the two outer long edges. The other end is rounded.
    const auto end = std::find_if(outer.begin() + 2, outer.end(),
        [&](const Edge& edge) { return touches(edge, outer[0]) && touches(edge, outer[1]); });
    if (end == outer.end()) throw std::runtime_error("Straight end not found.");
    return {outer[0], outer[1], *end, inner[0], inner[1]};
}

int main(int argc, char* argv[]) {
    const bool noGui = argc == 2 && std::string(argv[1]) == "--no-gui";
    if (argc != 1 && !noGui) {
        std::cerr << "Usage: edge_segment_extraction [--no-gui]\n";
        return 2;
    }
    try {
        const cv::Mat image = loadImage("assets/workpiece.png");
        cv::Mat gray;
        cv::cvtColor(image, gray, cv::COLOR_BGR2GRAY);
        cv::GaussianBlur(gray, gray, cv::Size(3, 3), 0);

        const auto segments = extractSegments(gray);
        cv::Mat marked = image.clone();
        for (const auto& edge : segments) {
            cv::line(marked, edge.first, edge.second, cv::Scalar(0, 255, 255), 1, cv::LINE_AA);
            std::cout << edge.first << " -> " << edge.second << ", " << edge.length << " px\n";
        }
        std::cout << "Segments: " << segments.size() << '\n';
        fs::create_directories("results");
        if (!cv::imwrite("results/straight_edges.png", marked)) {
            throw std::runtime_error("Cannot save results.");
        }
        if (!noGui) {
            cv::Mat preview;
            cv::resize(marked, preview, {}, 4, 4, cv::INTER_NEAREST);
            cv::imshow("Workpiece Straight Edges", preview);
            cv::waitKey(0);
            cv::destroyAllWindows();
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
