#include "Brain/CounterPick.hpp"
#include "Logger.hpp"
#include <fstream>
#include <sstream>

bool CounterPick::LoadFromJson(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) { Logger::Log("CounterPick file not found: " + path); return false; }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::stringstream ss(line);
        std::string hero, enemy, scoreStr;
        if (std::getline(ss, hero, ',') && std::getline(ss, enemy, ',') && std::getline(ss, scoreStr)) {
            matchups[hero][enemy] = std::stof(scoreStr);
        }
    }
    Logger::Log("CounterPick loaded, heroes: " + std::to_string(matchups.size()));
    return true;
}

std::string CounterPick::GetBestPick(const std::vector<std::string>& enemies,
                                     const std::vector<std::string>& availablePool) {
    if (availablePool.empty()) return "";
    if (enemies.empty()) return availablePool[0];
    std::string best = availablePool[0];
    float bestScore = 0.5f;
    for (const auto& hero : availablePool) {
        float score = 0.5f;
        auto it = matchups.find(hero);
        if (it != matchups.end()) {
            for (const auto& enemy : enemies) {
                auto jt = it->second.find(enemy);
                if (jt != it->second.end()) score += jt->second;
            }
        }
        if (score > bestScore) { bestScore = score; best = hero; }
    }
    return best;
}