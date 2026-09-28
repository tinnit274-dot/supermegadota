#include "Vision/GameStateExtractor.hpp"
#include "Logger.hpp"
#include <filesystem>
namespace fs = std::filesystem;

bool GameStateExtractor::LoadTemplates(const std::string& dir) {
    auto load = [&](const std::string& name, cv::Mat& out) {
        std::string path = dir + "/" + name;
        out = cv::imread(path, cv::IMREAD_COLOR);
        if (out.empty()) { Logger::Log("Failed to load template: " + path); return false; }
        return true;
    };
    if (!load("find_match.png", templFindMatch)) return false;
    if (!load("accept.png", templAccept)) return false;
    if (!load("pick_phase.png", templPickPhase)) return false;
    if (!load("hp_bar.png", templHpBar)) return false;
    load("lock_in.png", templLockIn); // optional
    std::string heroesDir = dir + "/heroes";
    if (fs::exists(heroesDir)) {
        for (const auto& entry : fs::directory_iterator(heroesDir)) {
            if (entry.is_regular_file()) {
                cv::Mat icon = cv::imread(entry.path().string(), cv::IMREAD_COLOR);
                if (!icon.empty()) heroTemplates.emplace_back(entry.path().stem().string(), icon);
            }
        }
    }
    Logger::Log("Templates loaded: " + std::to_string(heroTemplates.size()) + " heroes");
    return true;
}

GameState GameStateExtractor::Extract(const cv::Mat& frame, const dota_bot::GSIState& gsi) {
    GameState state;
    state.lastFrame = frame.clone();
    state.gsi = gsi;
    state.hasGSI = gsi.valid;

    if (!frame.empty()) {
        state.hasFindMatchButton = TemplateMatcher::Find(frame, templFindMatch, 0.75).confidence > 0;
        state.hasAcceptButton = TemplateMatcher::Find(frame, templAccept, 0.75).confidence > 0;
        state.isPicking = TemplateMatcher::Find(frame, templPickPhase, 0.7).confidence > 0;
        state.isInGame = TemplateMatcher::Find(frame, templHpBar, 0.7).confidence > 0;
    }

    if (gsi.valid) {
        if (gsi.gameState == "DOTA_GAMERULES_STATE_HERO_SELECTION") { state.phase = GamePhase::HeroPick; state.isPicking = true; }
        else if (gsi.gameState == "DOTA_GAMERULES_STATE_GAME_IN_PROGRESS" || gsi.gameState == "DOTA_GAMERULES_STATE_PRE_GAME") { state.phase = GamePhase::InGame; state.isInGame = true; }
        else if (gsi.gameState == "DOTA_GAMERULES_STATE_POST_GAME") state.phase = GamePhase::PostGame;
        else if (gsi.gameState == "DOTA_GAMERULES_STATE_WAIT_FOR_PLAYERS_TO_LOAD" ||
                 gsi.gameState == "DOTA_GAMERULES_STATE_PRE_GAME" ||
                 gsi.gameState == "DOTA_GAMERULES_STATE_STRATEGY_TIME") state.phase = GamePhase::Loading;
    }

    if (state.phase == GamePhase::Unknown && !frame.empty()) {
        if (state.hasAcceptButton) state.phase = GamePhase::MatchFound;
        else if (state.isPicking) state.phase = GamePhase::HeroPick;
        else if (state.isInGame) state.phase = GamePhase::InGame;
        else if (state.hasFindMatchButton) state.phase = GamePhase::MainMenu;
    }
    return state;
}

std::vector<std::string> GameStateExtractor::DetectEnemyPicks(const cv::Mat& frame) {
    std::vector<std::string> enemies;
    if (frame.empty()) return enemies;
    for (const auto& [name, icon] : heroTemplates) {
        auto matches = TemplateMatcher::FindAll(frame, icon, 0.85);
        for (const auto& m : matches) {
            if (m.center.x > frame.cols * 0.5) { enemies.push_back(name); break; }
        }
    }
    return enemies;
}