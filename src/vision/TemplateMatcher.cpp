#include "Vision/TemplateMatcher.hpp"
#include <cmath>

MatchResult TemplateMatcher::Find(const cv::Mat& screen, const cv::Mat& templ, double threshold) {
    if (screen.empty() || templ.empty()) return { cv::Point(0,0), 0.0, cv::Rect(0,0,0,0) };
    if (templ.cols > screen.cols || templ.rows > screen.rows)
        return { cv::Point(0,0), 0.0, cv::Rect(0,0,0,0) };
    cv::Mat result;
    cv::matchTemplate(screen, templ, result, cv::TM_CCOEFF_NORMED);
    double minVal, maxVal;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    if (std::isfinite(maxVal) && maxVal >= threshold) {
        cv::Rect r(maxLoc.x, maxLoc.y, templ.cols, templ.rows);
        return { cv::Point(maxLoc.x + templ.cols/2, maxLoc.y + templ.rows/2), maxVal, r };
    }
    return { cv::Point(0,0), 0.0, cv::Rect(0,0,0,0) };
}

std::vector<MatchResult> TemplateMatcher::FindAll(const cv::Mat& screen, const cv::Mat& templ, double threshold) {
    std::vector<MatchResult> matches;
    if (screen.empty() || templ.empty()) return matches;
    if (templ.cols > screen.cols || templ.rows > screen.rows) return matches;
    cv::Mat result;
    cv::matchTemplate(screen, templ, result, cv::TM_CCOEFF_NORMED);
    for (int y = 0; y < result.rows; ++y) {
        for (int x = 0; x < result.cols; ++x) {
            float val = result.at<float>(y, x);
            if (std::isfinite(val) && val >= threshold) {
                bool tooClose = false;
                cv::Point center(x + templ.cols/2, y + templ.rows/2);
                for (auto& m : matches) {
                    if (cv::norm(center - m.center) < std::max(templ.cols, templ.rows)) {
                        tooClose = true; break;
                    }
                }
                if (!tooClose) matches.push_back({ center, val, cv::Rect(x, y, templ.cols, templ.rows) });
            }
        }
    }
    return matches;
}
