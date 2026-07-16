#include "bot.hpp"
#include "bot_incompat.hpp"
#include "macro_conversion.hpp"
#include "../practice_fixes/practice_fixes.hpp"
#include "../ui/game/game_ui.hpp"
#include "../ui/layers/record_layer.hpp"

#include <cmath>
#include <random>

namespace {
bool g_botBootstrapping = false;
}

Bot::Bot() {
    g_botBootstrapping = true;
    Settings::applyDefaults();
    Settings::loadRuntimeState(*this);
    g_botBootstrapping = false;
}

bool Bot::isBootstrapping() {
    return g_botBootstrapping;
}

bool Bot::hasIncompatibleMods() {
    return bot_incompat::hasIncompatibleMods();
}

bool Bot::enabledIncompatibleGDSettings() {
    return bot_incompat::enabledIncompatibleGDSettings();
}

bool Bot::isAutoSafeModeActive() {
#ifdef XDBOT_DISABLE_AUTO_SAFE_MODE
    return false;
#else
    auto& bot = Bot::get();
    return bot.safeMode && bot.mod->getSavedValue<bool>("macro_auto_safe_mode");
#endif
}

float Bot::getTPS() {
    auto& bot = Bot::get();
    return bot.tpsEnabled ? bot.tps : 240.f;
}

int Bot::getCurrentFrame(bool editor) {
    auto& bot = Bot::get();

    int frame = bot.attemptStartFrame + bot.updater.frameCount - bot.frameOffset;

    return std::max(frame, 0);
}

void Bot::updateSeed(bool isRestart) {
    auto& bot = Bot::get();

    if (bot.seedEnabled) {
        PlayLayer* pl = PlayLayer::get();
        if (!pl)
            return;

        uint64_t seed =
            geode::utils::numFromString<uint64_t>(bot.mod->getSavedValue<std::string>("macro_seed"))
                .unwrapOr(0);

        uint64_t finalSeed;

        if (!pl->m_player1->m_isDead) {
            bot.gen.seed(seed + static_cast<uint64_t>(pl->m_gameState.m_currentProgress));

            finalSeed = bot.gen.generate(10000, 1000000000);
        } else {
            finalSeed = geode::utils::random::generate(1000, 1000000000);
        }

        GameToolbox::fast_srand(finalSeed);
        bot.safeMode = true;
    }

    if (isRestart && bot.state == state::recording) {
        uint64_t gdSeed = GameToolbox::getfast_srand();
        bot.replay.seed = gdSeed;

        bot.gen.seed(gdSeed);
    }
}

void Bot::updatePitch(float value) {
    auto& bot = Bot::get();
    if (!bot.speedhackAudio) {
        if (bot.currentPitch != 1.f)
            value = 1.f;
        else
            return;
    }

    FMODAudioEngine* fmod = FMODAudioEngine::sharedEngine();
    FMOD::ChannelGroup* channel = nullptr;
    fmod->m_system->getMasterChannelGroup(&channel);

    if (channel) {
        channel->setPitch(value);
        bot.currentPitch = value;
    }
}

void Bot::recordAction(int frame, int button, bool player2, bool hold) {
    auto* pl = PlayLayer::get();
    if (!pl)
        return;

    auto& bot = Bot::get();

    if (bot.replay.inputs.empty())
        updateMacroInfo(pl);

    if (bot.tpsEnabled)
        bot.replay.framerate = bot.tps;

    player2 = !player2;

    for (int i = static_cast<int>(bot.replay.inputs.size()) - 1; i >= 0; --i) {
        auto& last = bot.replay.inputs[i];
        if (last.button != button || last.player2 != player2)
            continue;

        if (last.down == hold)
            return;
        break;
    }

    if (!hold) {
        bool hasPress = false;
        for (int i = static_cast<int>(bot.replay.inputs.size()) - 1; i >= 0; --i) {
            auto& input = bot.replay.inputs[i];
            if (input.button == button && input.player2 == player2 && input.down) {
                hasPress = true;
                break;
            }
        }

        if (!hasPress)
            return;
    }

    bot.replay.inputs.emplace_back(frame, button, player2, hold);
}

void Bot::regenerateClickRandomization() {
    auto& bot = Bot::get();
    bot.randomizedFrames.clear();

    if (bot.clickRandomization <= 0 || bot.state != state::playing)
        return;

    double framerate = bot.replay.framerate > 0.f ? bot.replay.framerate : 240.f;
    // Convert the ms jitter range into physics frames at the macro's framerate.
    int maxOffsetFrames = static_cast<int>(
        std::round(static_cast<double>(bot.clickRandomization) * framerate / 1000.0));
    if (maxOffsetFrames <= 0)
        return;

    bot.randomizedFrames.reserve(bot.replay.inputs.size());

    uint64_t previous = 0;
    // Last randomized frame per (button, player) so a hold/release pair can never
    // collapse onto the same frame or swap order.
    std::unordered_map<int, uint64_t> lastPerKey;

    for (auto const& input : bot.replay.inputs) {
        int offset = geode::utils::random::generate(-maxOffsetFrames, maxOffsetFrames);
        int64_t frame = static_cast<int64_t>(input.frame) + offset;

        frame = std::max<int64_t>(frame, 0);
        // Keep the overall sequence monotonic so playback consumes inputs in order.
        frame = std::max<int64_t>(frame, static_cast<int64_t>(previous));

        int key = input.button * 2 + (input.player2 ? 1 : 0);
        if (auto it = lastPerKey.find(key); it != lastPerKey.end())
            frame = std::max<int64_t>(frame, static_cast<int64_t>(it->second) + 1);

        previous = static_cast<uint64_t>(frame);
        lastPerKey[key] = previous;
        bot.randomizedFrames.push_back(previous);
    }
}

uint64_t Bot::getInputPlaybackFrame(size_t index) {
    auto& bot = Bot::get();
    if (index < bot.randomizedFrames.size())
        return bot.randomizedFrames[index];
    return bot.replay.inputs[index].frame;
}

void Bot::recordFrameFix(int frame, PlayerObject* p1, PlayerObject* p2) {
    auto normalizeRotation = [](float rotation) {
        while (rotation < 0.f || rotation > 360.f)
            rotation += rotation < 0.f ? 360.f : -360.f;
        return rotation;
    };

    gdr_legacy::FrameData p1Data;
    p1Data.pos = p1->getPosition();
    p1Data.rotation = normalizeRotation(p1->getRotation());
    p1Data.yVelocity = p1->m_yVelocity;
    p1Data.xVelocity = p1->m_platformerXVelocity;

    gdr_legacy::FrameData p2Data;
    if (p2) {
        p2Data.pos = p2->getPosition();
        p2Data.rotation = normalizeRotation(p2->getRotation());
        p2Data.yVelocity = p2->m_yVelocity;
        p2Data.xVelocity = p2->m_platformerXVelocity;
    }

    Bot::get().replay.frameFixes.push_back({frame, p1Data, p2Data});
}

void Bot::autoSave(GJGameLevel* level, int number) {
    if (!level) {
        auto* pl = PlayLayer::get();
        level = pl ? pl->m_level : nullptr;
    }
    if (!level)
        return;

    std::string levelName = level->m_levelName;
#ifdef GEODE_IS_IOS
    std::filesystem::path autoSavesPath = Mod::get()->getSaveDir() / "autosaves";
#else
    std::filesystem::path autoSavesPath = Mod::get()->getSettingValue<std::filesystem::path>("autosaves_folder");
#endif
    if (!std::filesystem::exists(autoSavesPath))
        return;

    std::filesystem::path path = autoSavesPath / fmt::format("autosave_{}_{}", levelName, number);
    std::string username =
        GJAccountManager::sharedState() ? GJAccountManager::sharedState()->m_username : "";

    int result = macro_conversion::save(
        Bot::get().replay,
        username,
        fmt::format("AutoSave {} in level {}", number, levelName),
        path,
        SaveFormat::GDR2);
    if (result != 0)
        log::debug("Failed to autosave macro. ID: {}. Path: {}", result, path);
}

void Bot::tryAutosave(GJGameLevel* level, CheckpointObject* cp) {
    auto& bot = Bot::get();

    if (bot.state != state::recording || !bot.autosaveEnabled || !bot.checkpoints.contains(cp))
        return;

#ifdef GEODE_IS_IOS
    std::filesystem::path autoSavesPath = bot.mod->getSaveDir() / "autosaves";
#else
    std::filesystem::path autoSavesPath = bot.mod->getSettingValue<std::filesystem::path>("autosaves_folder");
#endif
    if (!std::filesystem::exists(autoSavesPath)) {
        log::debug("Failed to access auto saves path.");
        return;
    }

    std::filesystem::path path =
        autoSavesPath / fmt::format("autosave_{}_{}", level->m_levelName, bot.currentSession);
    std::error_code ec;
    std::filesystem::remove(path.string() + ".gdr2", ec);
    if (ec)
        log::warn("Failed to remove previous autosave");

    autoSave(level, bot.currentSession);
}

void Bot::updateMacroInfo(PlayLayer* pl) {
    if (!pl)
        return;

    auto& replay = Bot::get().replay;
    auto* level = pl->m_level;

    replay.ldm = level->m_lowDetailModeToggled;
    replay.levelInfo.id = static_cast<uint32_t>(level->m_levelID.value());
    replay.levelInfo.name = level->m_levelName;
    replay.author = GJAccountManager::sharedState()->m_username;
    if (replay.author.empty())
        replay.author = "N/A";

    replay.botInfo.name = "xdBot";
    replay.botInfo.version = getModVersionInt();
    replay.xdBotMacro = true;

    if (Bot::get().state == state::recording)
        replay.clickBetweenSteps = bot_incompat::clickBetweenStepsEnabled();
}

void Bot::updateMacroTPS() {
    auto& bot = Bot::get();

    if (bot.state != state::none && !bot.replay.inputs.empty()) {
        if (bot.previousTps == 0.f) {
            bot.previousTpsEnabled = bot.tpsEnabled;
            bot.previousTps = bot.tps;
        }

        bot.setTps(bot.replay.framerate);
        bot.setTpsEnabled(bot.replay.framerate != 240.f);
    } else if (bot.previousTps != 0.f) {
        bot.setTpsEnabled(bot.previousTpsEnabled);
        bot.setTps(bot.previousTps);
        bot.previousTps = 0.f;
    }

    if (bot.layer)
        static_cast<RecordLayer*>(bot.layer)->updateTPS();
}

void Bot::toggleRecording() {
    if (hasIncompatibleMods())
        return;

    auto& bot = Bot::get();
    if (bot.layer) {
        auto* layer = static_cast<RecordLayer*>(bot.layer);
        layer->recording->toggle(bot.state != state::recording);
        layer->toggleRecording(nullptr);
        return;
    }

    auto* layer = RecordLayer::create();
    layer->toggleRecording(nullptr);
    layer->onClose(nullptr);
}

void Bot::togglePlaying() {
    if (hasIncompatibleMods())
        return;

    auto& bot = Bot::get();
    if (bot.layer) {
        auto* layer = static_cast<RecordLayer*>(bot.layer);
        layer->playing->toggle(bot.state != state::playing);
        layer->togglePlaying(nullptr);
        return;
    }

    auto* layer = RecordLayer::create();
    layer->togglePlaying(nullptr);
    layer->onClose(nullptr);
}

void Bot::resetState(bool cp) {
    auto& bot = Bot::get();
    bot.restart = false;
    bot.state = state::none;
    bot.attemptStartFrame = 0;

    if (!cp)
        bot.checkpoints.clear();

    Interface::updateLabels();
    Interface::updateButtons();
}

bool Bot::shouldStep() {
    auto& bot = Bot::get();
    return bot.stepFrame || getCurrentFrame() == 0;
}

bool Bot::flipControls() {
    auto* pl = PlayLayer::get();
    if (!pl)
        return GameManager::get()->getGameVariable(GameVar::Flip2PlayerControls);

    return pl->m_isPlatformer ? false
                              : GameManager::get()->getGameVariable(GameVar::Flip2PlayerControls);
}

void Bot::frameStep() {
    auto& bot = Bot::get();
    if (!PlayLayer::get() || !bot.frameStepper)
        return;

    if (bot.suppressNextFrameStep) {
        bot.suppressNextFrameStep = false;
        return;
    }

    bot.stepFrame = true;
    bot.stepFrameDraw = true;
    bot.stepFrameParticle = 4;
}

void Bot::backstepFrame() {
    auto& bot = Bot::get();
    if (!PlayLayer::get() || !bot.frameStepper)
        return;

    if (!PracticeFix::backstepFrame()) {
        bot.suppressNextFrameStep = false;
        return;
    }

    bot.stepFrame = false;
    bot.suppressNextFrameStep = false;
    bot.stepFrameDraw = true;
    bot.stepFrameDrawMultiple = 2;
    bot.stepFrameParticle = 4;
}

void Bot::syncFrameStepperMusic() {
    auto* pl = PlayLayer::get();
    if (!pl)
        return;

    double levelTime = pl->m_gameState.m_levelTime;
    if (levelTime < 0.0)
        levelTime = 0.0;

    FMODAudioEngine::sharedEngine()->setMusicTimeMS(static_cast<unsigned int>(levelTime * 1000.0),
                                                    true, 0);
}

void Bot::toggleSpeedhack() {
    auto& bot = Bot::get();
    bot.mod->setSavedValue("macro_speedhack_enabled",
                           !bot.mod->getSavedValue<bool>("macro_speedhack_enabled"));
    bot.speedhackEnabled = bot.mod->getSavedValue<bool>("macro_speedhack_enabled");

    if (bot.layer) {
        if (static_cast<RecordLayer*>(bot.layer)->speedhackToggle)
            static_cast<RecordLayer*>(bot.layer)->speedhackToggle->toggle(
                bot.mod->getSavedValue<bool>("macro_speedhack_enabled"));
    }

    if (!bot.mod->getSavedValue<bool>("macro_speedhack_enabled"))
        Bot::updatePitch(1.f);
}

void Bot::toggleFrameStepper() {
    auto& bot = Bot::get();

    bot.frameStepper = !bot.frameStepper;

    bot.mod->setSavedValue("macro_frame_stepper", bot.frameStepper);

    bot.stepFrame = false;
    bot.suppressNextFrameStep = false;
    bot.stepFrameDraw = false;
    bot.stepFrameDrawMultiple = 0;
    bot.stepFrameParticle = false;

    PracticeFix::clearStoredFrames();

    if (!bot.frameStepper) {
        Bot::syncFrameStepperMusic();
    } else {
        PracticeFix::saveFrameStepperFrame();
    }

    if (auto rl =
        static_cast<RecordLayer*>(bot.layer)) {

        if (rl->frameStepperToggle)
            rl->frameStepperToggle->toggle(
                bot.frameStepper
            );
    }

    Interface::updateButtons();
}
