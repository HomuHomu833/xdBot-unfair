#pragma once

#include <Geode/loader/Mod.hpp>
#include <gdr/gdr.hpp>
#include <gdr_convert.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <vector>

#include <matjson.hpp>
#include <matjson/msgpack.hpp>

#include <Geode/utils/VersionInfo.hpp>

struct BotReplay;

std::string getModVersionString();

int getModVersionInt();

geode::prelude::VersionInfo getVersion(std::vector<std::string> nums);

cocos2d::CCPoint dataFromString(std::string dataString);

namespace gdr_legacy {

struct Bot {
    std::string name;
    std::string version;

    Bot(std::string const& name, std::string const& version);
};

struct Level {
    uint32_t id;
    std::string name;

    Level() = default;

    Level(std::string const& name, uint32_t id = 0);
};

struct FrameData {
    cocos2d::CCPoint pos = {0.f, 0.f};
    float rotation = 0.f;
    bool rotate = true;
    double yVelocity = 0.0;
    double xVelocity = 0.0;
};

struct FrameFix {
    int frame;
    FrameData p1;
    FrameData p2;
};

} // namespace gdr_legacy

struct ReplayInput : gdr::Input<> {
    ReplayInput() = default;

    ReplayInput(uint64_t frame, uint8_t button, bool player2, bool down)
        : Input(frame, button, player2, down) {}

    bool operator==(const ReplayInput& other) const {
        return frame == other.frame && player2 == other.player2 && button == other.button &&
               down == other.down;
    }

    bool operator<(const ReplayInput& other) const {
        return frame < other.frame;
    }
};

using input = ReplayInput;

struct BotReplay : gdr::Replay<BotReplay, ReplayInput> {
    BotReplay()
        : Replay("xdBot", 1) {
        botInfo.name = "xdBot";
    }

    bool canChangeFPS = true;
    uintptr_t seed = 0;
    bool xdBotMacro = true;
    bool isLegacy = false;
    // Whether this macro was recorded with Click Between Steps enabled. Persisted
    // in the GDR2 extension block (appended last for backwards compatibility).
    bool clickBetweenSteps = false;

    std::vector<gdr_legacy::FrameFix> frameFixes;

    bool shouldParseExtension() const override {
        return botInfo.name == "xdBot";
    }

    void parseExtension(binary_reader& reader) override;
    void saveExtension(binary_writer& writer) const override;

    std::string getBotVersionString() const;

    gdr::Result<std::vector<uint8_t>> exportGDR2();

    std::vector<uint8_t> exportGDR1();

    std::vector<uint8_t> exportJSON();
};

namespace gdr_legacy {

class LegacyMacro;
struct legacy_input;

class Input {
  public:
    uint32_t frame;
    int button;
    bool player2;
    bool down;

    Input();
    Input(int frame, int button, bool player2, bool down);

    virtual void parseExtension(matjson::Value const& obj);
    virtual matjson::Value saveExtension() const;

    static Input hold(int frame, int button, bool player2 = false);
    static Input release(int frame, int button, bool player2 = false);

    bool operator<(Input const& other) const;
};

class LegacyMacro {
  public:
    std::string author;
    std::string description;

    float duration;
    float gameVersion;
    float version = 1.0;

    float framerate = 240.f;

    int seed = 0;
    int coins = 0;

    bool ldm = false;

    Bot botInfo;
    Level levelInfo;

    std::vector<legacy_input> inputs;
    std::vector<FrameFix> frameFixes;

    LegacyMacro();

    uint32_t frameForTime(double time) const;

    static LegacyMacro importData(std::vector<uint8_t> const& data, bool importInputs = true);

    std::vector<uint8_t> exportData(bool exportJson = false) const;
};

struct legacy_input : Input {
    legacy_input();

    legacy_input(int frame, int button, bool player2, bool down);
};

int normalizeXDVersion(int version);

int parseBotVersion(std::string version);

void normalizeReplayVersion(BotReplay& replay);

LegacyMacro toLegacy(const BotReplay& replay);

BotReplay fromLegacy(const LegacyMacro& legacy);

BotReplay importReplay(std::vector<uint8_t>& data);

BotReplay importXD(std::filesystem::path path);

} // namespace gdr_legacy
