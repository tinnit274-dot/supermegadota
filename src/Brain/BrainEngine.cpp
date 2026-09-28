#include "Brain/BrainEngine.hpp"
#include "Logger.hpp"
#include <algorithm>
#include <cmath>

BrainEngine::BrainEngine() : env(ORT_LOGGING_LEVEL_WARNING, "DotaBot") {
    sessionOptions.SetIntraOpNumThreads(4);
    sessionOptions.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
    memoryInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
}

bool BrainEngine::LoadModel(const std::string& path) {
    try {
        session.reset();
        inputNameStorage.clear();
        outputNameStorage.clear();
        inputNames.clear();
        outputNames.clear();
        inputShapeImg.clear();
        inputShapeFeat.clear();
        hasGSIInput = false;

        std::wstring wpath(path.begin(), path.end());
        session = std::make_unique<Ort::Session>(env, wpath.c_str(), sessionOptions);
        Ort::AllocatorWithDefaultOptions allocator;
        size_t numInputs = session->GetInputCount();
        for (size_t i = 0; i < numInputs; ++i) {
            auto name = session->GetInputNameAllocated(i, allocator);
            inputNameStorage.emplace_back(name.get());
            auto typeInfo = session->GetInputTypeInfo(i);
            auto tensorInfo = typeInfo.GetTensorTypeAndShapeInfo();
            auto shape = tensorInfo.GetShape();
            for (auto& dimension : shape)
                if (dimension <= 0) dimension = 1;
            if (shape.size() == 4) {
                imageInputIndex = i;
                inputShapeImg = shape;
            } else if (shape.size() == 2) {
                gsiInputIndex = i;
                inputShapeFeat = shape;
                hasGSIInput = true;
            }
        }
        size_t numOutputs = session->GetOutputCount();
        for (size_t i = 0; i < numOutputs; ++i)
        {
            auto name = session->GetOutputNameAllocated(i, allocator);
            outputNameStorage.emplace_back(name.get());
        }
        for (auto& name : inputNameStorage) inputNames.push_back(name.c_str());
        for (auto& name : outputNameStorage) outputNames.push_back(name.c_str());
        Logger::Log("ONNX loaded: " + path + (hasGSIInput ? " (dual)" : " (image)"));
        return true;
    } catch (const Ort::Exception& e) {
        Logger::Log("ONNX load failed: " + std::string(e.what()));
        return false;
    }
}

std::vector<float> BrainEngine::EncodeGSI(const dota_bot::GSIState& gsi) const {
    std::vector<float> feat;
    if (!gsi.valid) {
        if (!inputShapeFeat.empty()) feat.resize(inputShapeFeat[1], 0.0f);
        return feat;
    }
    auto clamp01 = [](float value) { return std::clamp(value, 0.0f, 1.0f); };
    feat.push_back(clamp01(gsi.health / std::max(gsi.maxHealth, 1.0f)));
    feat.push_back(clamp01(gsi.mana / std::max(gsi.maxMana, 1.0f)));
    feat.push_back(clamp01(static_cast<float>(gsi.gold) / 10000.0f));
    feat.push_back(clamp01(static_cast<float>(gsi.heroLevel) / 30.0f));
    feat.push_back(gsi.isAlive ? 1.0f : 0.0f);
    feat.push_back(static_cast<float>(gsi.respawnSeconds) / 100.0f);
    for (int i = 0; i < 6; ++i) {
        if (i < static_cast<int>(gsi.abilities.size())) {
            feat.push_back(gsi.abilities[i].canCast ? 1.0f : 0.0f);
            feat.push_back(gsi.abilities[i].cooldown / 100.0f);
        } else {
            feat.push_back(0.0f);
            feat.push_back(0.0f);
        }
    }
    for (int i = 0; i < 6; ++i) {
        feat.push_back(i < static_cast<int>(gsi.items.size()) && !gsi.items[i].name.empty() ? 1.0f : 0.0f);
    }
    feat.push_back(gsi.gameTime / 3600.0f);
    feat.push_back(std::clamp(gsi.clockTime / 3600.0f, 0.0f, 1.0f));
    feat.push_back(std::clamp(static_cast<float>(gsi.kills) / 50.0f, 0.0f, 1.0f));
    feat.push_back(std::clamp(static_cast<float>(gsi.deaths) / 50.0f, 0.0f, 1.0f));
    feat.push_back(std::clamp(static_cast<float>(gsi.assists) / 50.0f, 0.0f, 1.0f));
    feat.push_back(std::clamp(static_cast<float>(gsi.lastHits) / 500.0f, 0.0f, 1.0f));
    feat.push_back(std::clamp(static_cast<float>(gsi.denies) / 100.0f, 0.0f, 1.0f));
    feat.push_back(std::clamp(gsi.posX / 10000.0f, -1.0f, 1.0f));
    feat.push_back(std::clamp(gsi.posY / 10000.0f, -1.0f, 1.0f));
    feat.push_back(gsi.valid ? 1.0f : 0.0f);
    size_t expected = inputShapeFeat.empty() ? feat.size() : static_cast<size_t>(inputShapeFeat[1]);
    if (feat.size() < expected) feat.resize(expected, 0.0f);
    else if (feat.size() > expected) feat.resize(expected);
    return feat;
}

BotAction BrainEngine::Predict(const cv::Mat& frame, const dota_bot::GSIState& gsi) {
    BotAction action;
    if (!session) return action;
    try {
        if (frame.empty() || inputShapeImg.size() != 4) return action;
        const int imageSize = inputShapeImg[2] > 0 ? static_cast<int>(inputShapeImg[2]) : 224;
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(imageSize, imageSize));
        cv::cvtColor(resized, resized, cv::COLOR_BGR2RGB);
        resized.convertTo(resized, CV_32F, 1.0 / 255.0);
        std::vector<float> imgTensor(3 * imageSize * imageSize);
        const float mean[3] = {0.485f, 0.456f, 0.406f};
        const float stdev[3] = {0.229f, 0.224f, 0.225f};
        for (int c = 0; c < 3; ++c) {
            for (int h = 0; h < imageSize; ++h) {
                for (int w = 0; w < imageSize; ++w) {
                    imgTensor[c * imageSize * imageSize + h * imageSize + w] =
                        (resized.at<cv::Vec3f>(h, w)[c] - mean[c]) / stdev[c];
                }
            }
        }
        const size_t numInputs = session->GetInputCount();
        std::vector<Ort::Value> inputValues;
        inputValues.reserve(numInputs);
        for (size_t i = 0; i < numInputs; ++i) inputValues.emplace_back(nullptr);
        inputValues[imageInputIndex] = Ort::Value::CreateTensor<float>(
            memoryInfo, imgTensor.data(), imgTensor.size(),
            inputShapeImg.data(), inputShapeImg.size());
        if (hasGSIInput) {
            auto gsiFeat = EncodeGSI(gsi);
            inputValues[gsiInputIndex] = Ort::Value::CreateTensor<float>(
                memoryInfo, gsiFeat.data(), gsiFeat.size(),
                inputShapeFeat.data(), inputShapeFeat.size());
        }
        auto outputs = session->Run(Ort::RunOptions{nullptr},
            inputNames.data(), inputValues.data(), inputValues.size(),
            outputNames.data(), outputNames.size());
        float* out = outputs[0].GetTensorMutableData<float>();
        auto outputShape = outputs[0].GetTensorTypeAndShapeInfo().GetShape();
        const size_t outputSize = outputShape.empty() ? 0 :
            static_cast<size_t>(outputShape.back());
        if (outputSize >= 13) {
            // The trainer exports five action logits, two target coordinates,
            // and six ability/item logits.
            int bestAction = 0;
            float bestLogit = out[0];
            for (int i = 1; i < 5; ++i) {
                if (out[i] > bestLogit) { bestLogit = out[i]; bestAction = i; }
            }
            float sum = 0.0f;
            for (int i = 0; i < 5; ++i) sum += std::exp(out[i] - bestLogit);
            action.confidence = 1.0f / std::max(sum, 1.0e-6f);
            action.type = static_cast<BotAction::Type>(bestAction);
            const auto sigmoid = [](float value) {
                return 1.0f / (1.0f + std::exp(-value));
            };
            action.targetX = sigmoid(out[5]);
            action.targetY = sigmoid(out[6]);
            action.mouseX = action.targetX;
            action.mouseY = action.targetY;
            action.abilitySlot = 0;
            for (int i = 1; i < 6; ++i)
                if (out[7 + i] > out[7 + action.abilitySlot]) action.abilitySlot = i;
        } else if (outputSize >= 7) {
            // Backward-compatible reader for the original regression model.
            int actionType = static_cast<int>(std::round(out[0]));
            action.type = static_cast<BotAction::Type>(std::clamp(actionType, 0, 9));
            action.mouseX = std::clamp(out[1], 0.0f, 1.0f);
            action.mouseY = std::clamp(out[2], 0.0f, 1.0f);
            action.targetX = std::clamp(out[3], 0.0f, 1.0f);
            action.targetY = std::clamp(out[4], 0.0f, 1.0f);
            action.abilitySlot = static_cast<int>(std::round(out[5]));
            action.confidence = std::clamp(out[6], 0.0f, 1.0f);
        }
    } catch (const std::exception& e) {
        Logger::Log("Predict error: " + std::string(e.what()));
    }
    return action;
}
