#pragma once
#include <string>
#include <vector>

namespace dota_bot {

struct GSIAbility {
    std::string name;
    int level = 0;
    bool canCast = false;
    float cooldown = 0;
    bool ultimate = false;
};

struct GSIItem {
    std::string name;
    bool canCast = false;
    int charges = 0;
};

struct GSIDraftPlayer {
    int id = 0;
    std::string name;
    std::string hero;
    bool pickConfirmed = false;
    bool isEnemy = false;
};

struct GSIState {
    bool valid = false;
    std::string gameState;
    std::string heroName;
    int heroLevel = 0;
    float health = 0, maxHealth = 1;
    float mana = 0, maxMana = 1;
    int gold = 0;
    int kills = 0, deaths = 0, assists = 0;
    int lastHits = 0, denies = 0;
    float posX = 0, posY = 0;
    bool isAlive = true;
    int respawnSeconds = 0;
    std::vector<GSIAbility> abilities;
    std::vector<GSIItem> items;
    std::vector<GSIDraftPlayer> draftAllies;
    std::vector<GSIDraftPlayer> draftEnemies;
    float gameTime = 0;
    float clockTime = 0;
};

} // namespace dota_bot