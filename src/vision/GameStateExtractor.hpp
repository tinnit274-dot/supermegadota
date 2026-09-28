#pragma once
#include "TemplateMatcher.hpp"
#include "GSI/GSITypes.hpp"
#include <string>

enum class GamePhase {
    Unknown, MainMenu, FindingMatch, MatchFound, HeroPick, Loading, InGame, PostGame
};

struct GameState {
    GamePhase phase = GamePhase::Unknown;
    bool hasFindMatchButton = false;
    bool hasAcceptButton = false;
    bool isPicking = false;
    bool isInGame = false;
    cv::Mat lastFrame;
    dota_bot::GSIState gsi;
    bool hasGSI = false;
};

class GameStateExtractor {
    cv::Mat templFindMatch, templAccept, templPickPhase, templHpBar, templLockIn;
    std::vector<std::pair<std::string, cv::Mat>> heroTemplates;
public:
    bool LoadTemplates(const std::string& dir);
    GameState Extract(const cv::Mat& frame, const dota_bot::GSIState& gsi = {});
    std::vector<std::string> DetectEnemyPicks(const cv::Mat& frame);
    const std::vector<std::pair<std::string, cv::Mat>>& GetHeroTemplates() const { return heroTemplates; }
};