#include "Demo/DemoRecorder.hpp"
#include "Logger.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
namespace fs = std::filesystem;
using json = nlohmann::json;

std::string DemoRecorder::DetectAction(const std::vector<BYTE>& keys, int mx, int my, int& outAbility) {
    outAbility = -1;
    if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) return "move";
    if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return "attack";
    for (BYTE k : keys) {
        if (k == 65) return "attack";
        if (k == 77 || k == 81) return "move";
        if (k == 83 || k == 72) return "none";
        if (k == 90) { outAbility = 0; return "ability"; }
        if (k == 88) { outAbility = 1; return "ability"; }
        if (k == 67) { outAbility = 2; return "ability"; }
        if (k == 69) { outAbility = 3; return "ability"; }
        if (k == 70) { outAbility = 4; return "ability"; }
        if (k == 82) { outAbility = 5; return "ability"; }
        if (k == 68) { outAbility = 0; return "item"; }
        if (k == 87) { outAbility = 1; return "item"; }
        if (k == 86) { outAbility = 2; return "item"; }
        if (k == VK_CAPITAL) { outAbility = 3; return "item"; }
        if (k == 66) { outAbility = 4; return "item"; }
        if (k == VK_XBUTTON1 || k == VK_XBUTTON2) { outAbility = 5; return "item"; }
    }
    return "none";
}

bool DemoRecorder::Start(const std::string& outputDir) {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", std::localtime(&t));
    sessionPath = outputDir + "/demo_" + buf;
    fs::create_directories(sessionPath + "/frames");
    logFile.open(sessionPath + "/actions.jsonl");
    if (!logFile.is_open()) return false;
    recording = true; frameCounter = 0;
    prevMouseX = 0; prevMouseY = 0;
    Logger::Log("Demo recording started: " + sessionPath);
    return true;
}

void DemoRecorder::RecordFrame(const cv::Mat& frame, const InputLog& log) {
    if (!recording) return;
    std::lock_guard<std::mutex> lock(fileMutex);
    int idx = frameCounter++;
    std::string imgPath = sessionPath + "/frames/frame_" + std::to_string(idx) + ".jpg";
    cv::imwrite(imgPath, frame);
    json j;
    j["frame"] = idx;
    j["ts"] = log.timestamp;
    j["mx"] = log.mouseX;
    j["my"] = log.mouseY;
    j["tx"] = log.targetX;
    j["ty"] = log.targetY;
    j["keys"] = log.keysDown;
    j["action"] = log.action;
    j["ability"] = log.ability;
    j["camx"] = log.camX;
    j["camy"] = log.camY;
    logFile << j.dump() << "\n";
}

void DemoRecorder::RecordGSI(const std::string& gsiJson) {
    if (!recording) return;
    std::lock_guard<std::mutex> lock(fileMutex);
    std::ofstream f(sessionPath + "/gsi.jsonl", std::ios::app);
    if (!f.is_open()) return;
    try {
        // Keep the original payload under "data" and add a local timestamp.
        // This allows the trainer to align asynchronous GSI updates to frames.
        auto payload = json::parse(gsiJson);
        json wrapped;
        wrapped["ts"] = GetTickCount64();
        wrapped["data"] = std::move(payload);
        f << wrapped.dump() << "\n";
    } catch (...) {
        f << gsiJson << "\n";
    }
}

void DemoRecorder::Stop() {
    if (!recording) return;
    recording = false;
    if (logFile.is_open()) logFile.close();
    Logger::Log("Demo recording stopped. Frames: " + std::to_string(frameCounter));
}

void DemoRecorder::StandaloneLoop() {
    int w = GetSystemMetrics(SM_CXSCREEN);
    int h = GetSystemMetrics(SM_CYSCREEN);
    while (standaloneRunning) {
        HDC hScreen = GetDC(NULL);
        if (!hScreen) { Sleep(100); continue; }
        HDC hDC = CreateCompatibleDC(hScreen);
        HBITMAP hBitmap = CreateCompatibleBitmap(hScreen, w, h);
        HGDIOBJ oldObj = SelectObject(hDC, hBitmap);
        BitBlt(hDC, 0, 0, w, h, hScreen, 0, 0, SRCCOPY);
        BITMAPINFOHEADER bi = {0};
        bi.biSize = sizeof(BITMAPINFOHEADER);
        bi.biWidth = w; bi.biHeight = -h; bi.biPlanes = 1;
        bi.biBitCount = 32; bi.biCompression = BI_RGB;
        std::vector<BYTE> buf(w * h * 4);
        GetDIBits(hDC, hBitmap, 0, h, buf.data(), (BITMAPINFO*)&bi, DIB_RGB_COLORS);
        cv::Mat mat(h, w, CV_8UC4, buf.data());
        cv::Mat bgr; cv::cvtColor(mat, bgr, cv::COLOR_BGRA2BGR);
        SelectObject(hDC, oldObj);
        DeleteObject(hBitmap); DeleteDC(hDC); ReleaseDC(NULL, hScreen);
        POINT pt; GetCursorPos(&pt);
        BYTE keys[256]; std::vector<BYTE> keysDown;
        if (GetKeyboardState(keys))
            for (int i = 0; i < 256; ++i)
                if (keys[i] & 0x80) keysDown.push_back(static_cast<BYTE>(i));
        int ability = -1;
        std::string action = DetectAction(keysDown, pt.x, pt.y, ability);
        InputLog log{GetTickCount64(), pt.x, pt.y, pt.x, pt.y, keysDown, action, ability, 0.5f, 0.5f};
        RecordFrame(bgr.clone(), log);
        Sleep(100);
    }
}

void DemoRecorder::StartStandalone(const std::string& outputDir) {
    if (standaloneRunning) return;
    if (!Start(outputDir)) return;
    standaloneRunning = true;
    recordThread = std::thread(&DemoRecorder::StandaloneLoop, this);
    Logger::Log("Standalone recording started");
}

void DemoRecorder::StopStandalone() {
    if (!standaloneRunning) return;
    standaloneRunning = false;
    if (recordThread.joinable()) recordThread.join();
    Stop();
    Logger::Log("Standalone recording stopped");
}
