#pragma once
#include "Vision/ScreenCapture.hpp"
#include "Vision/GameStateExtractor.hpp"
#include "Brain/BrainEngine.hpp"
#include "Brain/CounterPick.hpp"
#include "Input/InputEmulator.hpp"
#include "Demo/DemoRecorder.hpp"
#include "GSI/GSIReceiver.hpp"
#include <thread>
#include <atomic>
#include <mutex>
#include <filesystem>

enum class BotStatus {
    Idle, WaitingForGame, FindingMatch, Accepting, PickingHero, InGame, Recording, Training
};

class BotApp {
    ScreenCapture capture;
    GameStateExtractor extractor;
    BrainEngine brain;
    CounterPick counterPick;
    InputEmulator input;
    DemoRecorder recorder;
    dota_bot::GSIReceiver gsiReceiver;
    std::atomic<bool> running{false};
    std::atomic<BotStatus> status{BotStatus::Idle};
    std::thread worker;
    std::thread trainWorker;
    std::mutex frameMutex;
    cv::Mat lastFrame;
    GameState lastState;
    std::string templatesDir = "templates";
    std::string modelPath = "models/model.onnx";
    std::string demosDir = "demos";
    std::string rootDir = ".";
    std::string dataDir = "data";

    void MainLoop();
    void HandleMainMenu(const GameState& state);
    void HandleMatchFound(const GameState& state);
    void HandleHeroPick(const GameState& state, const cv::Mat& frame);
    void HandleInGame(const cv::Mat& frame, const dota_bot::GSIState& gsi);
    void HandleInGameCVOnly(const cv::Mat& frame);
    void HandleInGameRuleBased(const cv::Mat& frame, const dota_bot::GSIState& gsi);

public:
    ~BotApp();
    bool Initialize();
    void StartBot();
    void StopBot();
    void StartRecording();
    void StopRecording();
    void TrainOnDemos();
    BotStatus GetStatus() const { return status.load(); }
    std::string GetStatusString() const;
    cv::Mat GetLastFrame();
    std::string GetRootDir() const { return rootDir; }
    std::string GetDemosDir() const { return demosDir; }
    std::string GetModelsDir() const;
    bool IsRunning() const { return running.load(); }
};
