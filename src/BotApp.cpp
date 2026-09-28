#include "BotApp.hpp"
#include "Logger.hpp"
#include <Windows.h>
#include <filesystem>
#include <random>
#include <chrono>
#include <thread>
#include <cstdlib>

namespace fs = std::filesystem;

namespace {
std::string GetExecutableDir() {
    char buffer[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return ".";
    return fs::path(std::string(buffer, len)).parent_path().string();
}

std::string GetProjectRoot() {
    const fs::path executableDir = GetExecutableDir();
    if (fs::exists(executableDir / "python" / "train.py")) {
        return executableDir.string();
    }
    const fs::path parent = executableDir.parent_path();
    if (fs::exists(parent / "python" / "train.py")) {
        return parent.string();
    }
    return executableDir.string();
}

std::string JoinPath(const std::string& base, const std::string& child) {
    return (fs::path(base) / child).lexically_normal().string();
}
}

BotApp::~BotApp() {
    StopBot();
    if (trainWorker.joinable()) trainWorker.join();
    gsiReceiver.Stop();
    capture.Release();
}

bool BotApp::Initialize() {
    try {
        Logger::Init();
        Logger::Log("=== Initialize started ===");

        // The release executable lives in build/, while project data and the
        // Python environment live one level above it.
        rootDir = GetProjectRoot();
        templatesDir = JoinPath(rootDir, "templates");
        modelPath = JoinPath(rootDir, "models/model.onnx");
        demosDir = JoinPath(rootDir, "demos");
        dataDir = JoinPath(rootDir, "data");

        fs::create_directories(JoinPath(rootDir, "models"));
        fs::create_directories(demosDir);
        fs::create_directories(dataDir);
        Logger::Log("Folders OK");

        Logger::Log("Starting GSI on :3000...");
        gsiReceiver.Start(3000);
        Logger::Log("GSI started");

        gsiReceiver.SetRawCallback([this](const std::string& raw) {
            if (recorder.IsRecording()) recorder.RecordGSI(raw);
        });

        Logger::Log("Init screen capture...");
        if (!capture.Initialize()) {
            Logger::Log("Screen capture init returned false");
        } else {
            Logger::Log("Screen capture OK");
        }

        if (!fs::exists(templatesDir)) {
            Logger::Log("WARNING: no templates folder");
        } else {
            Logger::Log("Loading templates...");
            extractor.LoadTemplates(templatesDir);
        }

        Logger::Log("Loading model...");
        if (!brain.LoadModel(modelPath) &&
            !brain.LoadModel(JoinPath(rootDir, "models/brain.onnx"))) {
            Logger::Log("No model found, will use rule-based AI");
        }

        counterPick.LoadFromJson(JoinPath(dataDir, "counterpick.csv"));
        Logger::Log("=== Initialize DONE ===");
        return true;
    } catch (const std::exception& e) {
        Logger::Log("INIT EXCEPTION: " + std::string(e.what()));
        return false;
    } catch (...) {
        Logger::Log("INIT UNKNOWN EXCEPTION");
        return false;
    }
}

void BotApp::StartBot() {
    try {
        if (running) return;
        running = true;
        status = BotStatus::WaitingForGame;
        Logger::Log("Creating thread...");
        worker = std::thread(&BotApp::MainLoop, this);
        Logger::Log("Thread created OK");
    } catch (const std::exception& e) {
        Logger::Log("STARTBOT EXCEPTION: " + std::string(e.what()));
        MessageBoxA(nullptr, e.what(), "StartBot Error", MB_OK);
        running = false;
    }
}

void BotApp::StopBot() {
    running = false;
    if (worker.joinable()) worker.join();
    status = BotStatus::Idle;
    Logger::Log("Bot stopped");
}

void BotApp::StartRecording() {
    recorder.StartStandalone(demosDir);
    status = recorder.IsRecording() ? BotStatus::Recording : BotStatus::Idle;
}
void BotApp::StopRecording() { recorder.StopStandalone(); status = BotStatus::Idle; }

void BotApp::TrainOnDemos() {
    if (status == BotStatus::Training) return;
    status = BotStatus::Training;
    Logger::Log("Training started...");
    const std::string script = JoinPath(rootDir, "python/train.py");
    const std::string output = JoinPath(rootDir, "models");
    const std::string venvPython = JoinPath(rootDir, "venv/Scripts/python.exe");
    const std::string python = fs::exists(venvPython) ? venvPython : "python";

    // Training is deliberately detached from the UI thread.  The status remains
    // "Training" until the process exits and the newly exported model is loaded.
    if (trainWorker.joinable()) trainWorker.join();
    trainWorker = std::thread([this, python, script, output]() {
        std::string cmd = "\"" + python + "\" \"" + script + "\" --demos \"" +
            demosDir + "\" --output \"" + output + "\" --epochs 20";
        const int result = std::system(cmd.c_str());
        if (result == 0) {
            brain.LoadModel(JoinPath(output, "model.onnx"));
            Logger::Log("Training completed and model reloaded");
        } else {
            Logger::Log("Training process failed with code " + std::to_string(result));
        }
        status = BotStatus::Idle;
    });
}

std::string BotApp::GetStatusString() const {
    switch (status.load()) {
        case BotStatus::Idle: return "Idle";
        case BotStatus::WaitingForGame: return "Waiting for Dota 2...";
        case BotStatus::FindingMatch: return "Finding match";
        case BotStatus::Accepting: return "Accepting match";
        case BotStatus::PickingHero: return "Picking hero";
        case BotStatus::InGame: return "Playing";
        case BotStatus::Recording: return "Recording demo";
        case BotStatus::Training: return "Training model";
        default: return "Unknown";
    }
}

cv::Mat BotApp::GetLastFrame() {
    std::lock_guard<std::mutex> lock(frameMutex);
    return lastFrame.clone();
}

std::string BotApp::GetModelsDir() const {
    return JoinPath(rootDir, "models");
}

void BotApp::MainLoop() {
    try {
        Logger::Log(">>> MainLoop STARTED");
        status = BotStatus::WaitingForGame;
        for (int i = 10; i > 0 && running; --i) {
            Logger::Log("Countdown: " + std::to_string(i));
            Sleep(1000);
        }

        Logger::Log("Countdown done, entering game loop");
        cv::Mat frame;
        int loopCounter = 0;

        // Кэш для логов по изменениям
        bool lastGSIValid = false;
        int lastPhase = -1;
        std::string lastHero;
        std::string lastGameState;

        while (running) {
            loopCounter++;

            auto gsiState = gsiReceiver.GetLatestState();

            // Логируем только по изменениям
            if (gsiState.valid != lastGSIValid) {
                Logger::Log("GSI valid changed: " + std::string(gsiState.valid ? "YES" : "NO"));
                lastGSIValid = gsiState.valid;
            }
            if (gsiState.gameState != lastGameState && !gsiState.gameState.empty()) {
                Logger::Log("GSI gameState: " + gsiState.gameState);
                lastGameState = gsiState.gameState;
            }
            if (gsiState.heroName != lastHero && !gsiState.heroName.empty()) {
                Logger::Log("GSI hero: " + gsiState.heroName);
                lastHero = gsiState.heroName;
            }

            bool captured = false;
            try {
                captured = capture.Capture(frame);
            } catch (const std::exception& e) {
                Logger::Log("Capture exception: " + std::string(e.what()));
            } catch (...) {
                Logger::Log("Capture unknown exception");
            }

            if (!captured) {
                static auto lastCapLog = std::chrono::steady_clock::time_point{};
                auto now = std::chrono::steady_clock::now();
                if (now - lastCapLog > std::chrono::seconds(2)) {
                    Logger::Log("Capture failed");
                    lastCapLog = now;
                }
                Sleep(100);
                continue;
            }

            if (frame.empty()) {
                Sleep(100);
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(frameMutex);
                lastFrame = frame.clone();
            }

            auto state = extractor.Extract(frame, gsiState);
            lastState = state;

            if (static_cast<int>(state.phase) != lastPhase) {
                Logger::Log("Phase changed to: " + std::to_string(static_cast<int>(state.phase)));
                lastPhase = static_cast<int>(state.phase);
            }

            // Форсированный режим из GSI
            if (state.phase == GamePhase::Unknown && gsiState.valid) {
                if (gsiState.gameState.find("GAME_IN_PROGRESS") != std::string::npos) {
                    state.phase = GamePhase::InGame;
                    state.isInGame = true;
                } else if (gsiState.gameState.find("HERO_SELECTION") != std::string::npos) {
                    state.phase = GamePhase::HeroPick;
                    state.isPicking = true;
                } else if (gsiState.gameState.find("POST_GAME") != std::string::npos) {
                    state.phase = GamePhase::PostGame;
                }
            }

            if (recorder.IsRecording()) {
                POINT pt; GetCursorPos(&pt);
                InputLog log;
                log.timestamp = GetTickCount64();
                log.mouseX = pt.x; log.mouseY = pt.y;
                BYTE keys[256];
                if (GetKeyboardState(keys)) {
                    for (int i = 0; i < 256; ++i)
                        if (keys[i] & 0x80) log.keysDown.push_back(static_cast<BYTE>(i));
                }
                if (!recorder.IsStandalone()) recorder.RecordFrame(frame, log);
            }

            switch (state.phase) {
                case GamePhase::MainMenu:
                    status = BotStatus::FindingMatch;
                    HandleMainMenu(state);
                    break;
                case GamePhase::MatchFound:
                    status = BotStatus::Accepting;
                    HandleMatchFound(state);
                    break;
                case GamePhase::HeroPick:
                    status = BotStatus::PickingHero;
                    HandleHeroPick(state, frame);
                    break;
                case GamePhase::InGame:
                    status = BotStatus::InGame;
                    HandleInGame(frame, gsiState);
                    break;
                case GamePhase::PostGame:
                case GamePhase::Loading:
                case GamePhase::FindingMatch:
                default:
                    Sleep(500);
                    break;
            }
            Sleep(100);
        }
        Logger::Log(">>> MainLoop ENDED");
    } catch (const std::exception& e) {
        Logger::Log("MAINLOOP EXCEPTION: " + std::string(e.what()));
        MessageBoxA(nullptr, e.what(), "Bot Crashed", MB_OK);
        running = false;
    } catch (...) {
        Logger::Log("MAINLOOP UNKNOWN EXCEPTION");
        MessageBoxA(nullptr, "Unknown error in bot thread", "Bot Crashed", MB_OK);
        running = false;
    }
}

void BotApp::HandleMainMenu(const GameState& state) {
    static auto lastClick = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (now - lastClick < std::chrono::seconds(5)) return;

    if (!state.hasFindMatchButton) {
        return;
    }

    cv::Mat templ = cv::imread(templatesDir + "/find_match.png", cv::IMREAD_COLOR);
    if (templ.empty()) {
        Logger::Log("HandleMainMenu: find_match.png is missing; waiting");
        return;
    }
    if (state.lastFrame.empty()) return;

    auto match = TemplateMatcher::Find(state.lastFrame, templ, 0.75);
    if (match.confidence > 0) {
        Logger::Log("Clicking Find Match at (" + std::to_string(match.center.x) + "," + std::to_string(match.center.y) + ")");
        input.ClickAtBezier(match.center.x, match.center.y);
        lastClick = now;
    }
}

void BotApp::HandleMatchFound(const GameState& state) {
    static auto lastClick = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (now - lastClick < std::chrono::seconds(3)) return;

    if (!state.hasAcceptButton) {
        return;
    }

    cv::Mat templ = cv::imread(templatesDir + "/accept.png", cv::IMREAD_COLOR);
    if (templ.empty()) {
        Logger::Log("HandleMatchFound: accept.png is missing; waiting");
        return;
    }
    if (state.lastFrame.empty()) return;

    auto match = TemplateMatcher::Find(state.lastFrame, templ, 0.75);
    if (match.confidence > 0) {
        Logger::Log("Clicking Accept");
        input.ClickAtBezier(match.center.x, match.center.y);
        lastClick = now;
    }
}

void BotApp::HandleHeroPick(const GameState& state, const cv::Mat& frame) {
    static auto lastAction = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (now - lastAction < std::chrono::seconds(2)) return;

    std::vector<std::string> enemies;
    for (const auto& p : state.gsi.draftEnemies)
        if (!p.hero.empty()) enemies.push_back(p.hero);

    std::vector<std::string> pool = { "antimage", "crystal_maiden", "pudge" };
    std::string pick = counterPick.GetBestPick(enemies, pool);
    if (pick.empty()) pick = pool[0];

    Logger::Log("HandleHeroPick: pick=" + pick + " enemies=" + std::to_string(enemies.size()));

    // First try to locate the selected hero from the loaded portrait templates.
    // A blind random click can lock an unintended hero, so it is intentionally
    // avoided.  The user can add portraits to templates/heroes to enable picks.
    for (const auto& [name, icon] : extractor.GetHeroTemplates()) {
        if (name != pick || icon.empty()) continue;
        const auto match = TemplateMatcher::Find(frame, icon, 0.82);
        if (match.confidence > 0.0) {
            input.ClickAtBezier(match.center.x, match.center.y);
            lastAction = now;
            Logger::Log("Hero selected: " + pick);
            Sleep(250);
            cv::Mat lockTemplate = cv::imread(templatesDir + "/lock_in.png", cv::IMREAD_COLOR);
            if (!lockTemplate.empty()) {
                const auto lock = TemplateMatcher::Find(frame, lockTemplate, 0.78);
                if (lock.confidence > 0.0) input.ClickAtBezier(lock.center.x, lock.center.y);
            }
            return;
        }
    }

    // If the portrait is missing, wait for a real match instead of clicking
    // arbitrary coordinates.  This makes the default build safe to test.
    Logger::Log("Hero portrait not found for " + pick + "; waiting");
}

void BotApp::HandleInGame(const cv::Mat& frame, const dota_bot::GSIState& gsi) {
    static int tickCounter = 0;
    tickCounter++;
    if (tickCounter % 50 == 0) {
        Logger::Log("[InGame] tick=" + std::to_string(tickCounter) +
                    " alive=" + std::string(gsi.isAlive ? "Y" : "N") +
                    " hp=" + std::to_string((int)gsi.health) + "/" + std::to_string((int)gsi.maxHealth) +
                    " hero=" + gsi.heroName);
    }
    if (brain.HasModel()) {
        const auto action = brain.Predict(frame, gsi);
        const int x = static_cast<int>(action.targetX * frame.cols);
        const int y = static_cast<int>(action.targetY * frame.rows);
        if (action.confidence >= 0.55f && action.type != BotAction::None) {
            switch (action.type) {
                case BotAction::Move:
                    input.ClickAtBezier(x, y);
                    return;
                case BotAction::Attack:
                    input.AttackMove(x, y);
                    return;
                case BotAction::Cast:
                    input.PressAbility(action.abilitySlot);
                    return;
                case BotAction::UseItem:
                    input.PressItem(action.abilitySlot);
                    return;
                default:
                    break;
            }
        }
    }
    HandleInGameRuleBased(frame, gsi);
}

void BotApp::HandleInGameCVOnly(const cv::Mat& frame) {
    static std::mt19937 rng((unsigned)GetTickCount64());
    static auto lastMove = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastMove).count() < 1000) return;
    lastMove = now;
    std::uniform_int_distribution<int> dx(0, frame.cols), dy(0, frame.rows);
    Logger::Log("CV-only: attack-move to random");
    input.AttackMove(dx(rng), dy(rng));
}

void BotApp::HandleInGameRuleBased(const cv::Mat& frame, const dota_bot::GSIState& gsi) {
    static auto lastActionTime = std::chrono::steady_clock::time_point{};
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastActionTime).count();
    if (elapsed < 500) return;
    lastActionTime = now;

    if (gsi.valid && !gsi.isAlive) {
        static auto lastDeathLog = std::chrono::steady_clock::time_point{};
        if (now - lastDeathLog > std::chrono::seconds(5)) {
            Logger::Log("Dead, respawn in " + std::to_string(gsi.respawnSeconds) + "s");
            lastDeathLog = now;
        }
        return;
    }

    int screenW = frame.cols, screenH = frame.rows;
    if (screenW == 0 || screenH == 0) return;

    // 1. Низкое HP
    if (gsi.valid && gsi.maxHealth > 0) {
        float hpPct = gsi.health / gsi.maxHealth;
        if (hpPct < 0.30f) {
            static auto lastRetreat = std::chrono::steady_clock::time_point{};
            if (now - lastRetreat > std::chrono::seconds(3)) {
                Logger::Log("LOW HP " + std::to_string((int)(hpPct*100)) + "%, retreating");
                input.ClickAtBezier(screenW / 2, screenH / 2);
                lastRetreat = now;
            }
            return;
        }
    }

    // 2. Способности
    if (gsi.valid) {
        for (size_t i = 0; i < gsi.abilities.size(); ++i) {
            const auto& ab = gsi.abilities[i];
            if (ab.canCast && !ab.name.empty()) {
                Logger::Log("Cast ability " + std::to_string(i) + " (" + ab.name + ")");
                input.PressAbility((int)i);
                return;
            }
        }
    }

    // 3. Предметы
    if (gsi.valid) {
        for (size_t i = 0; i < gsi.items.size(); ++i) {
            const auto& it = gsi.items[i];
            if (it.canCast && !it.name.empty()) {
                Logger::Log("Use item " + std::to_string(i) + " (" + it.name + ")");
                input.PressItem((int)i);
                return;
            }
        }
    }

    // 4. Conservative fallback: keep the hero near the lane area.  The old
    // random target behaviour made the bot wander unpredictably and produced
    // unusable training labels.
    const int targetX = screenW / 2;
    const int targetY = static_cast<int>(screenH * 0.58f);
    input.AttackMove(targetX, targetY);
}
