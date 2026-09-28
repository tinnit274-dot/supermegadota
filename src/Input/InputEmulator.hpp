#pragma once
#include <Windows.h>
#include <vector>
#include <utility>
#include <string>
#include <algorithm>

class InputEmulator {
public:
    void MoveMouse(int x, int y);
    void MoveMouseBezier(int x0, int y0, int x1, int y1, float speed = 1.0f);
    void LeftClick();
    void RightClick();
    void KeyPress(BYTE vk);
    void KeyDown(BYTE vk);
    void KeyUp(BYTE vk);
    void ClickAt(int x, int y);
    void ClickAtBezier(int x, int y);
    void PressAbility(int slot);
    void PressItem(int slot);
    void AttackMove(int x, int y);
    void MoveCamera(int dx, int dy);
    void TypeText(const std::string& text, float wpm = 300.0f);
private:
    std::vector<std::pair<int,int>> GenerateBezier(int x0, int y0, int x1, int y1, int points);
    void SleepRandom(int minMs, int maxMs);
};