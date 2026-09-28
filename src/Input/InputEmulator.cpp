#include "Input/InputEmulator.hpp"
#include <cmath>
#include <random>

void InputEmulator::SleepRandom(int minMs, int maxMs) {
    static std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
    std::uniform_int_distribution<int> dist(minMs, maxMs);
    Sleep(dist(rng));
}

std::vector<std::pair<int,int>> InputEmulator::GenerateBezier(int x0, int y0, int x1, int y1, int points) {
    static std::mt19937 rng(static_cast<unsigned>(GetTickCount64()));
    std::uniform_int_distribution<int> dist(-100, 100);
    int cx1 = x0 + (x1 - x0) / 3 + dist(rng);
    int cy1 = y0 + (y1 - y0) / 3 + dist(rng) * 2;
    int cx2 = x0 + 2 * (x1 - x0) / 3 + dist(rng);
    int cy2 = y0 + 2 * (y1 - y0) / 3 + dist(rng) * 2;
    std::vector<std::pair<int,int>> result;
    for (int i = 0; i <= points; ++i) {
        float t = i / static_cast<float>(points);
        float mt = 1.0f - t;
        float x = mt*mt*mt*x0 + 3*mt*mt*t*cx1 + 3*mt*t*t*cx2 + t*t*t*x1;
        float y = mt*mt*mt*y0 + 3*mt*mt*t*cy1 + 3*mt*t*t*cy2 + t*t*t*y1;
        result.emplace_back(static_cast<int>(x), static_cast<int>(y));
    }
    return result;
}

void InputEmulator::MoveMouse(int x, int y) {
    int sx = (x * 65535) / GetSystemMetrics(SM_CXSCREEN);
    int sy = (y * 65535) / GetSystemMetrics(SM_CYSCREEN);
    INPUT inp = {}; inp.type = INPUT_MOUSE;
    inp.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    inp.mi.dx = sx; inp.mi.dy = sy;
    SendInput(1, &inp, sizeof(INPUT));
}

void InputEmulator::MoveMouseBezier(int x0, int y0, int x1, int y1, float speed) {
    float dist = std::sqrt(std::pow(x1-x0, 2) + std::pow(y1-y0, 2));
    int points = static_cast<int>(dist / (6.0f / speed));
    points = std::clamp(points, 15, 250);
    auto curve = GenerateBezier(x0, y0, x1, y1, points);
    for (size_t i = 0; i < curve.size(); ++i) {
        MoveMouse(curve[i].first, curve[i].second);
        float progress = i / static_cast<float>(curve.size());
        float delayFactor = 1.0f + 0.5f * std::sin(progress * 3.14159f);
        int delay = static_cast<int>((1.5f + delayFactor * 2.5f) / speed);
        SleepRandom(delay, delay + 2);
    }
}

void InputEmulator::LeftClick() {
    INPUT inp = {}; inp.type = INPUT_MOUSE; inp.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    SendInput(1, &inp, sizeof(INPUT));
    SleepRandom(40, 90);
    inp.mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(1, &inp, sizeof(INPUT));
}

void InputEmulator::RightClick() {
    INPUT inp = {}; inp.type = INPUT_MOUSE; inp.mi.dwFlags = MOUSEEVENTF_RIGHTDOWN;
    SendInput(1, &inp, sizeof(INPUT));
    SleepRandom(40, 90);
    inp.mi.dwFlags = MOUSEEVENTF_RIGHTUP;
    SendInput(1, &inp, sizeof(INPUT));
}

void InputEmulator::KeyPress(BYTE vk) { KeyDown(vk); SleepRandom(30, 80); KeyUp(vk); }
void InputEmulator::KeyDown(BYTE vk) { INPUT inp = {}; inp.type = INPUT_KEYBOARD; inp.ki.wVk = vk; SendInput(1, &inp, sizeof(INPUT)); }
void InputEmulator::KeyUp(BYTE vk) { INPUT inp = {}; inp.type = INPUT_KEYBOARD; inp.ki.wVk = vk; inp.ki.dwFlags = KEYEVENTF_KEYUP; SendInput(1, &inp, sizeof(INPUT)); }

void InputEmulator::ClickAt(int x, int y) { MoveMouse(x, y); LeftClick(); }
void InputEmulator::ClickAtBezier(int x, int y) {
    POINT cur; GetCursorPos(&cur);
    MoveMouseBezier(cur.x, cur.y, x, y);
    SleepRandom(80, 200);
    LeftClick();
}

void InputEmulator::PressAbility(int slot) {
    static const BYTE keys[] = { 'Q','W','E','R','D','F' };
    if (slot >= 0 && slot < 6) KeyPress(keys[slot]);
}

void InputEmulator::PressItem(int slot) {
    static const BYTE keys[] = { VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4, VK_NUMPAD5, VK_NUMPAD6 };
    if (slot >= 0 && slot < 6) KeyPress(keys[slot]);
}

void InputEmulator::AttackMove(int x, int y) {
    KeyDown('A'); SleepRandom(40, 80);
    ClickAtBezier(x, y); SleepRandom(40, 80); KeyUp('A');
}

void InputEmulator::MoveCamera(int dx, int dy) {
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    if (dx > 0) MoveMouseBezier(screenW - 5, screenH / 2, screenW - 5, screenH / 2);
    else if (dx < 0) MoveMouseBezier(5, screenH / 2, 5, screenH / 2);
    SleepRandom(200, 400);
}

void InputEmulator::TypeText(const std::string& text, float wpm) {
    float msPerChar = 60000.0f / wpm;
    for (char c : text) {
        SHORT vk = VkKeyScanA(c);
        if (vk == -1) continue;
        KeyPress(static_cast<BYTE>(vk & 0xFF));
        SleepRandom(static_cast<int>(msPerChar * 0.7f), static_cast<int>(msPerChar * 1.3f));
    }
}