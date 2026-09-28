#pragma once
#include <Windows.h>
#include <opencv2/opencv.hpp>
#include <string>
#include <fstream>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>

struct InputLog {
    uint64_t timestamp;
    int mouseX, mouseY;
    int targetX, targetY;
    std::vector<BYTE> keysDown;
    std::string action;
    int ability = -1;
    float camX = 0.5f;
    float camY = 0.5f;
};

class DemoRecorder {
    std::string sessionPath;
    std::ofstream logFile;
    std::atomic<int> frameCounter{0};
    std::atomic<bool> recording{false};
    std::thread recordThread;
    std::mutex fileMutex;
    std::atomic<bool> standaloneRunning{false};
    int prevMouseX = 0;
    int prevMouseY = 0;
    void StandaloneLoop();
    std::string DetectAction(const std::vector<BYTE>& keys, int mx, int my, int& outAbility);
public:
    bool Start(const std::string& outputDir);
    void RecordFrame(const cv::Mat& frame, const InputLog& log);
    void RecordGSI(const std::string& gsiJson);
    void Stop();
    bool IsRecording() const { return recording; }
    bool IsStandalone() const { return standaloneRunning; }
    void StartStandalone(const std::string& outputDir);
    void StopStandalone();
};
