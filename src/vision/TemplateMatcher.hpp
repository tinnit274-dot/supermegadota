#pragma once
#include <opencv2/opencv.hpp>
#include <vector>

struct MatchResult {
    cv::Point center;
    double confidence;
    cv::Rect rect;
};

class TemplateMatcher {
public:
    static MatchResult Find(const cv::Mat& screen, const cv::Mat& templ, double threshold = 0.8);
    static std::vector<MatchResult> FindAll(const cv::Mat& screen, const cv::Mat& templ, double threshold = 0.75);
};