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

struct Edge {
    cv::Point2f first;
    cv::Point2f second;
    double length;
};

std::vector<Edge> contourEdges(const std::vector<cv::Point>& contour, double tolerance) {
    std::vector<cv::Point> polygon;
    cv::approxPolyDP(contour, polygon, tolerance, true);
    std::vector<Edge> edges;
    for (size_t i = 0; i < polygon.size(); ++i) {
        const auto a = polygon[i];
        const auto b = polygon[(i + 1) % polygon.size()];
        edges.push_back({cv::Point2f(a), cv::Point2f(b), cv::norm(b - a)});
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

cv::Vec4f fitSlotSide(const cv::Mat& gray, const Edge& edge, cv::Point2f center) {
    const auto delta = edge.second - edge.first;
    cv::Point2f inward(-delta.y, delta.x);
    inward *= static_cast<float>(1.0 / edge.length);
    if (inward.dot(center - edge.first) < 0) inward *= -1;

    std::vector<cv::Point2f> samples;
    cv::Mat patch;
    // Search across the bright-to-dark transition away from the corners.
    for (int i = 2; i <= 8; ++i) {
        const auto p = edge.first + delta * (i / 10.0f);
        std::array<float, 9> intensities{};
        for (int k = -4; k <= 4; ++k) {
            const auto position = p + inward * static_cast<float>(k);
            if (position.x < 0 || position.x > gray.cols - 1 ||
                position.y < 0 || position.y > gray.rows - 1) {
                throw std::runtime_error("Slot edge is too close to the image border.");
            }
            cv::getRectSubPix(gray, {1, 1}, position, patch, CV_32F);
            intensities[k + 4] = patch.at<float>(0, 0);
        }
        std::array<float, 7> gradients{};
        for (size_t k = 0; k < gradients.size(); ++k) {
            gradients[k] = intensities[k] - intensities[k + 2];
        }
        const auto peak = std::max_element(gradients.begin(), gradients.end());
        if (*peak < 15 || peak == gradients.begin() || peak == gradients.end() - 1) continue;
        const float curvature = peak[-1] - 2 * *peak + peak[1];
        const float correction = curvature < 0 ? 0.5f * (peak[-1] - peak[1]) / curvature : 0;
        const float offset = static_cast<float>(peak - gradients.begin()) - 3 + correction;
        samples.push_back(p + inward * offset);
    }
    if (samples.size() < 4) throw std::runtime_error("Insufficient contrast along a slot edge.");
    cv::Vec4f line;
    cv::fitLine(samples, line, cv::DIST_HUBER, 0, 0.01, 0.01);
    return line;
}

cv::Point2f intersection(const cv::Vec4f& a, const cv::Vec4f& b) {
    const cv::Point2f directionA(a[0], a[1]), directionB(b[0], b[1]);
    const cv::Point2f originA(a[2], a[3]), originB(b[2], b[3]);
    const double denominator = directionA.cross(directionB);
    if (std::abs(denominator) < 0.25) throw std::runtime_error("Invalid slot corner.");
    const auto t = (originB - originA).cross(directionB) / denominator;
    return originA + directionA * static_cast<float>(t);
}

void refineSlot(const cv::Mat& gray, std::vector<Edge>& edges) {
    cv::Point2f center;
    for (const auto& edge : edges) center += edge.first;
    center *= 0.25f;
    std::array<cv::Vec4f, 4> lines;
    for (size_t i = 0; i < lines.size(); ++i) lines[i] = fitSlotSide(gray, edges[i], center);
    std::array<cv::Point2f, 4> corners;
    for (size_t i = 0; i < corners.size(); ++i) {
        corners[i] = intersection(lines[(i + 3) % 4], lines[i]);
    }
    for (size_t i = 0; i < edges.size(); ++i) {
        const auto a = corners[i];
        const auto b = corners[(i + 1) % 4];
        edges[i] = {a, b, cv::norm(b - a)};
    }
}

std::vector<Edge> extractSegments(const cv::Mat& gray) {
    cv::Mat mask;
    cv::GaussianBlur(gray, mask, cv::Size(3, 3), 0);
    cv::threshold(mask, mask, 0, 255, cv::THRESH_BINARY | cv::THRESH_OTSU);
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
    refineSlot(gray, inner);
    sortByLength(inner);

    // The straight end joins the two outer long edges. The other end is rounded.
    const auto end = std::find_if(outer.begin() + 2, outer.end(),
        [&](const Edge& edge) { return touches(edge, outer[0]) && touches(edge, outer[1]); });
    if (end == outer.end()) throw std::runtime_error("Straight end not found.");
    return {outer[0], outer[1], *end, inner[0], inner[1]};
}

void drawSegments(cv::Mat& image, const std::vector<Edge>& segments, int scale) {
    constexpr int shift = 4;
    const auto pixel = [scale, shift](cv::Point2f point) {
        const float offset = (scale - 1) / 2.0f;
        return cv::Point(cvRound((point.x * scale + offset) * (1 << shift)),
                         cvRound((point.y * scale + offset) * (1 << shift)));
    };
    for (const auto& edge : segments) {
        cv::line(image, pixel(edge.first), pixel(edge.second),
                 cv::Scalar(0, 255, 255), scale == 1 ? 1 : 2, cv::LINE_AA, shift);
    }
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
        const auto segments = extractSegments(gray);
        cv::Mat marked = image.clone();
        drawSegments(marked, segments, 1);
        for (const auto& edge : segments) {
            std::cout << edge.first << " -> " << edge.second << ", " << edge.length << " px\n";
        }
        std::cout << "Segments: " << segments.size() << '\n';
        fs::create_directories("results");
        if (!cv::imwrite("results/straight_edges.png", marked)) {
            throw std::runtime_error("Cannot save results.");
        }
        if (!noGui) {
            cv::Mat preview;
            cv::resize(image, preview, {}, 4, 4, cv::INTER_LINEAR);
            drawSegments(preview, segments, 4);
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
