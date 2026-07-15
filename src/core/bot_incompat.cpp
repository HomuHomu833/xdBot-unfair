#include "bot_incompat.hpp"

#include "bot.hpp"
#include "../ui/game/game_ui.hpp"

#include <Geode/ui/Notification.hpp>

namespace {
struct IncompatibleSetting {
    std::string id;
    bool incompatValue;
    bool isModToggle = false;
    bool isSavedValue = false;
};

struct IncompatibleMod {
    std::string id;
    bool canBeDisabled;
    std::vector<IncompatibleSetting> incompatSettings;
};

const std::vector<IncompatibleMod> incompatibleMods{
    {"thesillydoggo.qolmod", true, {{"tps-bypass_enabled", true, false, true}}},
};

void resetBotStateOnIncompat() {
    auto& bot = Bot::get();
    bot.state = state::none;
    Interface::updateLabels();
    Interface::updateButtons();
}

} // namespace

void bot_incompat::restoreAutoDisabledSettings() {
    auto& bot = Bot::get();
    auto* mod = Loader::get()->getLoadedMod("syzzi.click_between_frames");
    if (mod && bot.clickBetweenFramesAutoDisabled) {
        mod->setSettingValue<bool>("soft-toggle", bot.clickBetweenFramesWasEnabled);
        bot.clickBetweenFramesAutoDisabled = false;
    }

    bot.cbsSupport = false;

    auto* gameManager = GameManager::sharedState();
    if (gameManager && bot.clickBetweenStepsAutoDisabled) {
        gameManager->setGameVariable(GameVar::ClickBetweenSteps,
                                     bot.clickBetweenStepsWasEnabled);
        bot.clickBetweenStepsAutoDisabled = false;
    }

    if (gameManager && bot.disableCheckpointsAutoDisabled) {
        gameManager->setGameVariable(GameVar::DisableCheckpoints,
                                     bot.disableCheckpointsWasEnabled);
        bot.disableCheckpointsAutoDisabled = false;
    }
}

void bot_incompat::autoDisableBotSettings() {
    auto& bot = Bot::get();

    if (auto* mod = Loader::get()->getLoadedMod("syzzi.click_between_frames")) {
        if (!bot.clickBetweenFramesAutoDisabled) {
            bot.clickBetweenFramesWasEnabled =
                mod->getSettingValue<bool>("soft-toggle");
            bot.clickBetweenFramesAutoDisabled = true;
        }
        mod->setSettingValue<bool>("soft-toggle", true);
    }

    auto* gameManager = GameManager::sharedState();
    if (gameManager) {
        // Click Between Steps is supported rather than blanket-disabled. While playing,
        // force CBS to match the state the macro was recorded with (enable it for CBS
        // macros, disable it otherwise) so playback stays deterministic; while recording,
        // simply mirror whatever the player currently has set. When bot.cbsSupport is on,
        // BotUpdater::runScheduler resolves every physics step individually so CBS's
        // per-step input timing is captured/reproduced. The previous value is saved so
        // restoreAutoDisabledSettings can put it back.
        bool cbsCurrentlyOn = clickBetweenStepsEnabled();
        bool wantCbs =
            (bot.state == state::playing) ? bot.replay.clickBetweenSteps : cbsCurrentlyOn;
        bot.cbsSupport = wantCbs;

        if (wantCbs != cbsCurrentlyOn) {
            if (!bot.clickBetweenStepsAutoDisabled) {
                bot.clickBetweenStepsWasEnabled =
                    gameManager->getGameVariable(GameVar::ClickBetweenSteps);
                bot.clickBetweenStepsAutoDisabled = true;
            }
            gameManager->setGameVariable(GameVar::ClickBetweenSteps, wantCbs);

            if (bot.state == state::playing && wantCbs)
                Notification::create("Enabled Click Between Steps for this macro",
                                     NotificationIcon::Info)
                    ->show();
        }

        if (auto* pl = PlayLayer::get(); pl && pl->m_isPlatformer) {
            if (!bot.disableCheckpointsAutoDisabled) {
                bot.disableCheckpointsWasEnabled =
                    gameManager->getGameVariable(GameVar::DisableCheckpoints);
                bot.disableCheckpointsAutoDisabled = true;
            }
            gameManager->setGameVariable(GameVar::DisableCheckpoints, false);
        }
    }
}

void showIncompatWarning(std::string const& title, std::vector<std::string> const& names) {
    geode::utils::StringBuffer incompatString;
    for (auto const& name : names)
        incompatString.append("<cr>{}</c>{}", name, (name != names.back() ? ", " : ""));

    FLAlertLayer::create("Warning", title + "\n" + incompatString.str(), "OK")->show();
}

void appendPrismIncompat(std::vector<std::string>& settingsToDisable) {
    auto* mod = Loader::get()->getLoadedMod("firee.prism");
    if (!mod)
        return;

    auto json = mod->getSavedValue<matjson::Value>("values");
    for (auto const& obj : json.asArray().unwrap()) {
        if (obj["name"].asString().unwrapOrDefault() != "TPS Bypass")
            continue;

        if (obj["value"].asInt().unwrapOrDefault() != 240)
            settingsToDisable.push_back("<cr>TPS Bypass (Prism Menu)</c>");
        break;
    }
}

void appendGDHIncompat(std::vector<std::string>& settingsToDisable) {
#ifdef GEODE_IS_WINDOWS
    constexpr auto gdhId = "tobyadd.gdh";
    constexpr auto key = "tps_enabled";
#else
    constexpr auto gdhId = "tobyadd.gdh_mobile";
    constexpr auto key = "fps_value";
#endif

    auto* mod = Loader::get()->getLoadedMod(gdhId);
    if (!mod)
        return;

    auto configPath = mod->getSaveDir() / "config.json";
    if (!std::filesystem::exists(configPath))
        return;

    auto jsonResult = geode::utils::file::readJson(configPath);
    if (!jsonResult)
        return;

    auto jsonData = jsonResult.unwrap();
    if (!jsonData.contains(key))
        return;

#ifdef GEODE_IS_WINDOWS
    if (jsonData[key].asBool())
        settingsToDisable.push_back("<cr>TPS Bypass (GDH)</c>");
#else
    auto fpsValue = jsonData[key].asDouble();
    if (fpsValue.isOk() && fpsValue.unwrap() != 240)
        settingsToDisable.push_back("<cr>TPS Bypass (GDH)</c>");
#endif
}

void appendConfiguredModIncompats(std::vector<std::string>& modsToDisable,
                                  std::vector<std::string>& settingsToDisable) {
    for (auto const& incompatMod : incompatibleMods) {
        auto* mod = Loader::get()->getLoadedMod(incompatMod.id);
        if (!mod)
            continue;

        auto modName = mod->getName();
        if (!incompatMod.canBeDisabled) {
            modsToDisable.push_back(modName);
            continue;
        }

        for (auto const& setting : incompatMod.incompatSettings) {
            bool value = setting.isSavedValue ? mod->getSavedValue<bool>(setting.id)
                                              : mod->getSettingValue<bool>(setting.id);
            if (value != setting.incompatValue)
                continue;

            if (setting.isModToggle) {
                modsToDisable.push_back(modName);
                continue;
            }

            auto settingName = setting.isSavedValue
                                   ? setting.id
                                   : mod->getSetting(setting.id)->getDisplayName();
            settingsToDisable.push_back(fmt::format("{} ({})", settingName, modName));
        }
    }
}

bool hasClickBetweenStepsLevelOverride() {
#ifndef GEODE_IS_MOBILE
    return false;
#else
    auto hasOverride = [](PlayLayer* playLayer) {
        return playLayer && playLayer->m_level->m_cbsOverride == 1;
    };

    if (hasOverride(PlayLayer::get()))
        return true;

    auto* scene = CCScene::get();
    if (!scene)
        return false;

    for (auto obj : scene->getChildrenExt<CCObject*>()) {
        if (auto* playLayer = typeinfo_cast<PlayLayer*>(obj); hasOverride(playLayer))
            return true;

        if (auto* levelInfo = typeinfo_cast<LevelInfoLayer*>(obj);
            levelInfo && levelInfo->m_level->m_cbsOverride == 1) {
            return true;
        }
    }

    return false;
#endif
}
namespace bot_incompat {

bool clickBetweenStepsEnabled() {
    auto* gameManager = GameManager::sharedState();
    bool gameVar = gameManager && gameManager->getGameVariable(GameVar::ClickBetweenSteps);
    return gameVar || hasClickBetweenStepsLevelOverride();
}

bool hasIncompatibleMods() {
    std::vector<std::string> modsToDisable;
    std::vector<std::string> settingsToDisable;

    appendPrismIncompat(settingsToDisable);
    appendGDHIncompat(settingsToDisable);
    appendConfiguredModIncompats(modsToDisable, settingsToDisable);

    if (!modsToDisable.empty())
        showIncompatWarning("The following mods are incompatible: ", modsToDisable);
    else if (!settingsToDisable.empty())
        showIncompatWarning("The following mod settings are incompatible: ", settingsToDisable);

    bool hasIncompat = !modsToDisable.empty() || !settingsToDisable.empty();
    if (hasIncompat)
        resetBotStateOnIncompat();

    return hasIncompat;
}

bool enabledIncompatibleGDSettings() {
    auto& bot = Bot::get();
    bool botActive = bot.state == state::recording || bot.state == state::playing;
    if (botActive) {
        auto previousIgnore = bot.ignoreRecordAction;
        bot.ignoreRecordAction = true;
        autoDisableBotSettings();
        bot.ignoreRecordAction = previousIgnore;
        return false;
    }

    // Click Between Steps is now supported (see autoDisableBotSettings), so it is no
    // longer treated as an incompatible GD setting.
    return false;
}

} // namespace bot_incompat
