#pragma once
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>
#include <vector>
#include <string>
#include <memory>
#include "GSI/GSITypes.hpp"

struct BotAction {
    enum Type { None=0, Move=1, Attack=2, Cast=3, UseItem=4, Buy=5, Chat=6, Camera=7, Idle=8, Retreat=9 };
    Type type = None;
    float mouseX = 0, mouseY = 0;
    float targetX = 0, targetY = 0;
    int abilitySlot = -1;
    float confidence = 0;
};

class BrainEngine {
    Ort::Env env;
    Ort::SessionOptions sessionOptions;
    std::unique_ptr<Ort::Session> session;
    Ort::MemoryInfo memoryInfo{nullptr};
    std::vector<std::string> inputNameStorage;
    std::vector<std::string> outputNameStorage;
    std::vector<const char*> inputNames;
    std::vector<const char*> outputNames;
    std::vector<int64_t> inputShapeImg;
    std::vector<int64_t> inputShapeFeat;
    size_t imageInputIndex = 0;
    size_t gsiInputIndex = 1;
    bool hasGSIInput = false;
    std::vector<float> EncodeGSI(const dota_bot::GSIState& gsi) const;

public:
    BrainEngine();
    bool LoadModel(const std::string& path);
    bool HasModel() const { return session != nullptr; }
    BotAction Predict(const cv::Mat& frame, const dota_bot::GSIState& gsi = {});
};
