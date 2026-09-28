#include "GSI/GSIReceiver.hpp"
#include "Logger.hpp"
#include "httplib.h"
#include "nlohmann/json.hpp"

#include <string>
#include <vector>
#include <utility>

using namespace dota_bot;
using json = nlohmann::json;

static const char* kGameStateNames[] = {
    "DOTA_GAMERULES_STATE_INIT",
    "DOTA_GAMERULES_STATE_WAIT_FOR_PLAYERS_TO_LOAD",
    "DOTA_GAMERULES_STATE_HERO_SELECTION",
    "DOTA_GAMERULES_STATE_STRATEGY_TIME",
    "DOTA_GAMERULES_STATE_PRE_GAME",
    "DOTA_GAMERULES_STATE_GAME_IN_PROGRESS",
    "DOTA_GAMERULES_STATE_POST_GAME"
};

bool GSIReceiver::Start(int port) {
    if (running.exchange(true)) return true;
    serverThread = std::thread(&GSIReceiver::RunServer, this, port);
    Logger::Log("GSI server starting on port " + std::to_string(port));
    return true;
}

void GSIReceiver::Stop() {
    running = false;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        if (server) server->stop();
    }
    if (serverThread.joinable()) serverThread.join();
    Logger::Log("GSI server stopped");
}

bool GSIReceiver::IsRunning() const { return running; }

GSIState GSIReceiver::GetLatestState() const {
    std::lock_guard<std::mutex> lock(stateMutex);
    return latestState;
}

void GSIReceiver::SetCallback(Callback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    callback = std::move(cb);
}
void GSIReceiver::SetRawCallback(RawCallback cb) {
    std::lock_guard<std::mutex> lock(callbackMutex);
    rawCallback = std::move(cb);
}

static void ParseHero(const json& j, GSIState& state) {
    if (!j.contains("hero") || !j["hero"].is_object()) return;
    auto& h = j["hero"];
    if (h.contains("name") && h["name"].is_string()) state.heroName = h["name"].get<std::string>();
    if (h.contains("level") && h["level"].is_number()) state.heroLevel = h["level"].get<int>();
    if (h.contains("health") && h["health"].is_number()) state.health = h["health"].get<float>();
    if (h.contains("max_health") && h["max_health"].is_number()) state.maxHealth = h["max_health"].get<float>();
    if (h.contains("mana") && h["mana"].is_number()) state.mana = h["mana"].get<float>();
    if (h.contains("max_mana") && h["max_mana"].is_number()) state.maxMana = h["max_mana"].get<float>();
    if (h.contains("x") && h["x"].is_number()) state.posX = h["x"].get<float>();
    if (h.contains("y") && h["y"].is_number()) state.posY = h["y"].get<float>();
    if (h.contains("alive") && h["alive"].is_boolean()) state.isAlive = h["alive"].get<bool>();
    if (h.contains("respawn_seconds") && h["respawn_seconds"].is_number())
        state.respawnSeconds = h["respawn_seconds"].get<int>();
}

static void ParsePlayer(const json& j, GSIState& state) {
    if (!j.contains("player") || !j["player"].is_object()) return;
    auto& p = j["player"];
    if (p.contains("gold") && p["gold"].is_number()) state.gold = p["gold"].get<int>();
    if (p.contains("kills") && p["kills"].is_number()) state.kills = p["kills"].get<int>();
    if (p.contains("deaths") && p["deaths"].is_number()) state.deaths = p["deaths"].get<int>();
    if (p.contains("assists") && p["assists"].is_number()) state.assists = p["assists"].get<int>();
    if (p.contains("last_hits") && p["last_hits"].is_number()) state.lastHits = p["last_hits"].get<int>();
    if (p.contains("denies") && p["denies"].is_number()) state.denies = p["denies"].get<int>();
}

static void ParseAbilities(const json& j, GSIState& state) {
    state.abilities.clear();
    if (!j.contains("abilities") || !j["abilities"].is_object()) return;
    for (auto& [key, val] : j["abilities"].items()) {
        if (!val.is_object()) continue;
        GSIAbility a;
        if (val.contains("name") && val["name"].is_string()) a.name = val["name"].get<std::string>();
        if (val.contains("level") && val["level"].is_number()) a.level = val["level"].get<int>();
        if (val.contains("can_cast") && val["can_cast"].is_boolean()) a.canCast = val["can_cast"].get<bool>();
        if (val.contains("cooldown") && val["cooldown"].is_number()) a.cooldown = val["cooldown"].get<float>();
        if (val.contains("ultimate") && val["ultimate"].is_boolean()) a.ultimate = val["ultimate"].get<bool>();
        state.abilities.push_back(a);
    }
}

static void ParseItems(const json& j, GSIState& state) {
    state.items.clear();
    if (!j.contains("items") || !j["items"].is_object()) return;
    for (auto& [key, val] : j["items"].items()) {
        if (!val.is_object()) continue;
        GSIItem item;
        if (val.contains("name") && val["name"].is_string()) item.name = val["name"].get<std::string>();
        if (val.contains("can_cast") && val["can_cast"].is_boolean()) item.canCast = val["can_cast"].get<bool>();
        if (val.contains("charges") && val["charges"].is_number()) item.charges = val["charges"].get<int>();
        state.items.push_back(item);
    }
}

static void ParseDraft(const json& j, GSIState& state) {
    state.draftAllies.clear();
    state.draftEnemies.clear();
    if (!j.contains("draft") || !j["draft"].is_object()) return;
    auto& d = j["draft"];
    if (!d.contains("teams") || !d["teams"].is_array()) return;
    auto& teams = d["teams"];
    for (size_t t = 0; t < teams.size() && t < 2; ++t) {
        if (!teams[t].is_object() || !teams[t].contains("players")) continue;
        if (!teams[t]["players"].is_array()) continue;
        for (auto& p : teams[t]["players"]) {
            if (!p.is_object()) continue;
            GSIDraftPlayer dp;
            if (p.contains("id") && p["id"].is_number()) dp.id = p["id"].get<int>();
            if (p.contains("name") && p["name"].is_string()) dp.name = p["name"].get<std::string>();
            if (p.contains("hero") && p["hero"].is_string()) dp.hero = p["hero"].get<std::string>();
            if (p.contains("pick_confirmed") && p["pick_confirmed"].is_boolean())
                dp.pickConfirmed = p["pick_confirmed"].get<bool>();
            dp.isEnemy = (t == 1);
            if (dp.isEnemy) state.draftEnemies.push_back(dp);
            else state.draftAllies.push_back(dp);
        }
    }
}

void GSIReceiver::RunServer(int port) {
    httplib::Server svr;
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        server = &svr;
    }

    svr.Post("/", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            RawCallback raw;
            Callback parsed;
            {
                std::lock_guard<std::mutex> lock(callbackMutex);
                raw = rawCallback;
                parsed = callback;
            }
            if (raw) raw(req.body);

            json j = json::parse(req.body);
            GSIState state;
            state.valid = true;

            // map
            if (j.contains("map") && j["map"].is_object()) {
                auto& m = j["map"];
                if (m.contains("game_state")) {
                    if (m["game_state"].is_string()) {
                        state.gameState = m["game_state"].get<std::string>();
                    } else if (m["game_state"].is_number()) {
                        int n = m["game_state"].get<int>();
                        if (n >= 0 && n < 7) state.gameState = kGameStateNames[n];
                        else state.gameState = std::to_string(n);
                    }
                }
                if (m.contains("game_time") && m["game_time"].is_number())
                    state.gameTime = m["game_time"].get<float>();
                if (m.contains("clock_time") && m["clock_time"].is_number())
                    state.clockTime = m["clock_time"].get<float>();
            }

            ParseHero(j, state);
            ParsePlayer(j, state);
            ParseAbilities(j, state);
            ParseItems(j, state);
            ParseDraft(j, state);

            {
                std::lock_guard<std::mutex> lock(stateMutex);
                latestState = state;
            }
            if (parsed) parsed(state);
            res.status = 200;
        } catch (const std::exception& e) {
            Logger::Log("GSI parse error: " + std::string(e.what()));
            Logger::Log("Body (first 500): " + req.body.substr(0, std::min<size_t>(500, req.body.size())));
            res.status = 400;
        }
    });

    Logger::Log("GSI server listening on :" + std::to_string(port));
    svr.listen("0.0.0.0", port);
    {
        std::lock_guard<std::mutex> lock(callbackMutex);
        server = nullptr;
    }
    running = false;
}
