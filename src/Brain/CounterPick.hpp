#pragma once
#include <string>
#include <vector>
#include <unordered_map>

class CounterPick {
    std::unordered_map<std::string, std::unordered_map<std::string, float>> matchups;
public:
    bool LoadFromJson(const std::string& path);
    std::string GetBestPick(const std::vector<std::string>& enemies,
                            const std::vector<std::string>& availablePool);
};