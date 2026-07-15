#include "gdr.hpp"

#include <Geode/Geode.hpp>

#include <optional>

using namespace geode::prelude;

namespace {
std::optional<matjson::Value> parseLegacyReplay(std::vector<uint8_t> const& data) {
    auto firstNonWhitespace = std::find_if(data.begin(), data.end(), [](uint8_t c) {
        return !std::isspace(static_cast<unsigned char>(c));
    });
    bool isJson =
        firstNonWhitespace != data.end() && (*firstNonWhitespace == '{' || *firstNonWhitespace == '[');

    auto tryJson = [&data]() {
        return matjson::parse(
            std::string_view(reinterpret_cast<char const*>(data.data()), data.size()));
    };
    auto tryMsgpack = [&data]() {
        return matjson::msgpack::parse(std::span<uint8_t const>(data.data(), data.size()));
    };

    auto parseResult = isJson ? tryJson() : tryMsgpack();
    if (parseResult)
        return parseResult.unwrap();

    parseResult = isJson ? tryMsgpack() : tryJson();
    if (parseResult)
        return parseResult.unwrap();

    return std::nullopt;
}

bool shouldUseRotation(std::string_view botName, std::string_view version) {
    if (botName != "xdBot")
        return false;

    if (version == "v2.0.0")
        return true;

    return version.find("beta.") == std::string_view::npos &&
           version.find("alpha.") == std::string_view::npos;
}

int getLegacyOffset(std::string_view botName, std::string_view version) {
    if (botName != "xdBot")
        return 0;

    std::string normalized(version);
    if (!normalized.empty() && normalized.front() == 'v')
        normalized.erase(normalized.begin());

    auto splitVer = geode::utils::string::split(normalized, ".");
    if (splitVer.size() > 3)
        return 1;

    return getVersion(splitVer) >= getVersion({"2", "3", "6"}) ? 0 : 1;
}

gdr_legacy::legacy_input parseInput(matjson::Value const& inputJson, int offset) {
    gdr_legacy::legacy_input input;
    input.frame = static_cast<uint32_t>(inputJson["frame"].asInt().unwrap()) + offset;
    if (auto value = inputJson["btn"].asInt())
        input.button = static_cast<int>(value.unwrap());
    if (auto value = inputJson["2p"].asBool())
        input.player2 = value.unwrap();
    if (auto value = inputJson["down"].asBool())
        input.down = value.unwrap();
    input.parseExtension(inputJson);
    return input;
}

std::optional<gdr_legacy::FrameFix> parseFrameFix(matjson::Value const& frameFixJson,
                                                  std::string_view botName,
                                                  bool allowRotation,
                                                  int offset) {
    auto frameValue = frameFixJson["frame"].asInt();
    if (!frameValue)
        return std::nullopt;

    gdr_legacy::FrameFix frameFix;
    frameFix.frame = static_cast<int>(frameValue.unwrap()) + offset;

    if (frameFixJson.contains("player1")) {
        if (auto value = frameFixJson["player1"].asString())
            frameFix.p1.pos = dataFromString(value.unwrap());
        if (auto value = frameFixJson["player2"].asString())
            frameFix.p2.pos = dataFromString(value.unwrap());
        frameFix.p1.rotate = false;
        frameFix.p2.rotate = false;
        return frameFix;
    }

    if (frameFixJson.contains("player1X")) {
        frameFix.p1.pos = ccp(static_cast<float>(frameFixJson["player1X"].asDouble().unwrapOr(0.0)),
                              static_cast<float>(frameFixJson["player1Y"].asDouble().unwrapOr(0.0)));
        frameFix.p2.pos = ccp(static_cast<float>(frameFixJson["player2X"].asDouble().unwrapOr(0.0)),
                              static_cast<float>(frameFixJson["player2Y"].asDouble().unwrapOr(0.0)));
        frameFix.p1.rotate = false;
        frameFix.p2.rotate = false;
        return frameFix;
    }

    if (!frameFixJson.contains("p1"))
        return std::nullopt;

    bool rotation = allowRotation && botName == "xdBot";

    auto const& p1 = frameFixJson["p1"];
    if (p1.contains("x"))
        frameFix.p1.pos.x = static_cast<float>(p1["x"].asDouble().unwrapOr(0.0));
    if (p1.contains("y"))
        frameFix.p1.pos.y = static_cast<float>(p1["y"].asDouble().unwrapOr(0.0));
    if (rotation && p1.contains("r"))
        frameFix.p1.rotation = static_cast<float>(p1["r"].asDouble().unwrapOr(0.0));

    if (!frameFixJson.contains("p2"))
        return frameFix;

    auto const& p2 = frameFixJson["p2"];
    if (p2.contains("x"))
        frameFix.p2.pos.x = static_cast<float>(p2["x"].asDouble().unwrapOr(0.0));
    if (p2.contains("y"))
        frameFix.p2.pos.y = static_cast<float>(p2["y"].asDouble().unwrapOr(0.0));
    if (rotation && p2.contains("r"))
        frameFix.p2.rotation = static_cast<float>(p2["r"].asDouble().unwrapOr(0.0));

    return frameFix;
}

matjson::Value saveFrameFix(gdr_legacy::FrameFix const& fix) {
    matjson::Value p1Json = matjson::makeObject({});
    matjson::Value p2Json = matjson::makeObject({});

    if (fix.p1.pos.x != 0.f)
        p1Json["x"] = fix.p1.pos.x;
    if (fix.p1.pos.y != 0.f)
        p1Json["y"] = fix.p1.pos.y;
    if (fix.p1.rotation != 0.f)
        p1Json["r"] = fix.p1.rotation;

    if (fix.p2.pos.x != 0.f)
        p2Json["x"] = fix.p2.pos.x;
    if (fix.p2.pos.y != 0.f)
        p2Json["y"] = fix.p2.pos.y;
    if (fix.p2.rotation != 0.f)
        p2Json["r"] = fix.p2.rotation;

    if (p1Json.size() == 0 && p2Json.size() == 0)
        return matjson::Value();

    matjson::Value frameFixJson = matjson::makeObject({});
    frameFixJson["frame"] = fix.frame;
    frameFixJson["p1"] = std::move(p1Json);
    if (p2Json.size() != 0)
        frameFixJson["p2"] = std::move(p2Json);

    return frameFixJson;
}

void normalizeImportedReplay(BotReplay& replay) {
    gdr_legacy::normalizeReplayVersion(replay);
    replay.xdBotMacro = replay.botInfo.name == "xdBot";
    replay.isLegacy = replay.botInfo.name == "xdBot" && replay.botInfo.version < 2600;
}
} // namespace

std::string getModVersionString() {
    return geode::Mod::get()->getVersion().toVString();
}

int getModVersionInt() {
    auto version = geode::Mod::get()->getVersion();
    return static_cast<int>(version.getMajor() * 1000 + version.getMinor() * 100 +
                            version.getPatch());
}

std::string BotReplay::getBotVersionString() const {
    if (botInfo.version <= 0)
        return "N/A";

    if (botInfo.name == "xdBot") {
        int major = botInfo.version / 1000;
        int minor = (botInfo.version / 100) % 10;
        int patch = botInfo.version % 100;
        return fmt::format("{}.{}.{}", major, minor, patch);
    }

    return geode::utils::numToString(botInfo.version);
}

gdr::Result<std::vector<uint8_t>> BotReplay::exportGDR2() {
    return gdr::Replay<BotReplay, ReplayInput>::exportData();
}

std::vector<uint8_t> BotReplay::exportGDR1() {
    auto legacy = gdr_legacy::toLegacy(*this);
    return legacy.exportData(false);
}

std::vector<uint8_t> BotReplay::exportJSON() {
    auto legacy = gdr_legacy::toLegacy(*this);
    return legacy.exportData(true);
}

void BotReplay::saveExtension(binary_writer& writer) const {
    writer << static_cast<uint64_t>(frameFixes.size());

    for (auto const& fix : frameFixes) {
        writer << static_cast<uint64_t>(fix.frame);
        writer << fix.p1.pos.x;
        writer << fix.p1.pos.y;
        writer << fix.p1.rotation;
        writer << fix.p1.rotate;
        writer << fix.p2.pos.x;
        writer << fix.p2.pos.y;
        writer << fix.p2.rotation;
        writer << fix.p2.rotate;
    }

    // Appended after the frame fixes so older readers (which stop after the fixes)
    // stay compatible, and macros saved before this field simply omit it.
    writer << clickBetweenSteps;
}

void BotReplay::parseExtension(binary_reader& reader) {
    uint64_t count = 0;
    reader >> count;

    frameFixes.clear();
    frameFixes.reserve(count);

    for (uint64_t i = 0; i < count; i++) {
        gdr_legacy::FrameFix fix;
        uint64_t frame = 0;
        reader >> frame;
        fix.frame = static_cast<int>(frame);

        reader >> fix.p1.pos.x;
        reader >> fix.p1.pos.y;
        reader >> fix.p1.rotation;
        reader >> fix.p1.rotate;
        reader >> fix.p2.pos.x;
        reader >> fix.p2.pos.y;
        reader >> fix.p2.rotation;
        reader >> fix.p2.rotate;

        frameFixes.push_back(fix);
    }

    // Optional trailing field; macros recorded before it omit it entirely.
    clickBetweenSteps = false;
    if (!reader.empty())
        reader >> clickBetweenSteps;
}

cocos2d::CCPoint dataFromString(std::string dataString) {
    auto parts = geode::utils::string::split(dataString, ",");
    float xPos =
        parts.size() > 1 ? geode::utils::numFromString<float>(parts[1]).unwrapOr(0.f) : 0.f;
    float yPos =
        parts.size() > 2 ? geode::utils::numFromString<float>(parts[2]).unwrapOr(0.f) : 0.f;
    return {xPos, yPos};
}

VersionInfo getVersion(std::vector<std::string> nums) {
    if (nums.size() < 3)
        return VersionInfo(0, 0, 0);

    size_t major = geode::utils::numFromString<int>(nums[0]).unwrapOr(0);
    size_t minor = geode::utils::numFromString<int>(nums[1]).unwrapOr(0);
    size_t patch = geode::utils::numFromString<int>(nums[2]).unwrapOr(0);
    return VersionInfo(major, minor, patch);
}

namespace gdr_legacy {
Bot::Bot(std::string const& name, std::string const& version)
    : name(name), version(version) {}

Level::Level(std::string const& name, uint32_t id)
    : id(id), name(name) {}

Input::Input()
    : frame(0), button(0), player2(false), down(false) {}

Input::Input(int frame, int button, bool player2, bool down)
    : frame(frame), button(button), player2(player2), down(down) {}

void Input::parseExtension(matjson::Value const&) {}

matjson::Value Input::saveExtension() const {
    return matjson::makeObject({});
}

Input Input::hold(int frame, int button, bool player2) {
    return Input(frame, button, player2, true);
}

Input Input::release(int frame, int button, bool player2) {
    return Input(frame, button, player2, false);
}

bool Input::operator<(Input const& other) const {
    return frame < other.frame;
}

legacy_input::legacy_input() = default;

legacy_input::legacy_input(int frame, int button, bool player2, bool down)
    : Input(frame, button, player2, down) {}

LegacyMacro::LegacyMacro()
    : botInfo("xdBot", getModVersionString()) {}

uint32_t LegacyMacro::frameForTime(double time) const {
    return static_cast<uint32_t>(time * static_cast<double>(framerate));
}

LegacyMacro LegacyMacro::importData(std::vector<uint8_t> const& data, bool importInputs) {
    LegacyMacro replay;
    auto parseResult = parseLegacyReplay(data);
    if (!parseResult.has_value())
        return replay;

    matjson::Value const& replayJson = parseResult.value();

    if (auto value = replayJson["gameVersion"].asDouble())
        replay.gameVersion = static_cast<float>(value.unwrap());
    if (auto value = replayJson["description"].asString())
        replay.description = value.unwrap();
    if (auto value = replayJson["version"].asDouble())
        replay.version = static_cast<float>(value.unwrap());
    if (auto value = replayJson["duration"].asDouble())
        replay.duration = static_cast<float>(value.unwrap());
    if (auto value = replayJson["author"].asString())
        replay.author = value.unwrap();
    if (auto value = replayJson["seed"].asInt())
        replay.seed = static_cast<int>(value.unwrap());
    if (auto value = replayJson["coins"].asInt())
        replay.coins = static_cast<int>(value.unwrap());
    if (auto value = replayJson["ldm"].asBool())
        replay.ldm = value.unwrap();
    if (auto value = replayJson["bot"]["name"].asString())
        replay.botInfo.name = value.unwrap();
    if (auto value = replayJson["bot"]["version"].asString())
        replay.botInfo.version = value.unwrap();
    if (auto value = replayJson["level"]["id"].asInt())
        replay.levelInfo.id = static_cast<uint32_t>(value.unwrap());
    if (auto value = replayJson["level"]["name"].asString())
        replay.levelInfo.name = value.unwrap();
    if (auto value = replayJson["framerate"].asDouble())
        replay.framerate = static_cast<float>(value.unwrap());

    if (!importInputs)
        return replay;

    std::string_view version = replay.botInfo.version;
    bool allowRotation = shouldUseRotation(replay.botInfo.name, version);
    int offset = getLegacyOffset(replay.botInfo.name, version);

    if (replayJson.contains("inputs") && replayJson["inputs"].isArray()) {
        for (auto const& inputJson : replayJson["inputs"].asArray().unwrap()) {
            if (!inputJson.contains("frame") || !inputJson["frame"].asInt())
                continue;
            replay.inputs.push_back(parseInput(inputJson, offset));
        }
    }

    if (!replayJson.contains("frameFixes") || !replayJson["frameFixes"].isArray())
        return replay;

    for (auto const& frameFixJson : replayJson["frameFixes"].asArray().unwrap()) {
        auto frameFix = parseFrameFix(frameFixJson, replay.botInfo.name, allowRotation, offset);
        if (frameFix)
            replay.frameFixes.push_back(std::move(frameFix.value()));
    }

    return replay;
}

std::vector<uint8_t> LegacyMacro::exportData(bool exportJson) const {
    matjson::Value replayJson = matjson::makeObject({});
    replayJson["gameVersion"] = gameVersion;
    replayJson["description"] = description;
    replayJson["version"] = version;
    replayJson["duration"] = duration;
    replayJson["bot"]["name"] = botInfo.name;
    replayJson["bot"]["version"] = botInfo.version;
    replayJson["level"]["id"] = static_cast<int>(levelInfo.id);
    replayJson["level"]["name"] = levelInfo.name;
    replayJson["author"] = author;
    replayJson["seed"] = seed;
    replayJson["coins"] = coins;
    replayJson["ldm"] = ldm;
    replayJson["framerate"] = framerate;

    auto inputsJson = std::vector<matjson::Value>{};
    inputsJson.reserve(inputs.size());
    for (auto const& input : inputs) {
        matjson::Value inputJson = input.saveExtension();
        inputJson["frame"] = static_cast<int>(input.frame);
        inputJson["btn"] = input.button;
        inputJson["2p"] = input.player2;
        inputJson["down"] = input.down;
        inputsJson.push_back(std::move(inputJson));
    }
    replayJson["inputs"] = std::move(inputsJson);

    auto frameFixesJson = std::vector<matjson::Value>{};
    frameFixesJson.reserve(frameFixes.size());
    for (auto const& frameFix : frameFixes) {
        auto frameFixJson = saveFrameFix(frameFix);
        if (!frameFixJson.isNull())
            frameFixesJson.push_back(std::move(frameFixJson));
    }
    replayJson["frameFixes"] = std::move(frameFixesJson);

    if (exportJson) {
        std::string serialized = replayJson.dump(matjson::NO_INDENTATION);
        return std::vector<uint8_t>(serialized.begin(), serialized.end());
    }

    return matjson::msgpack::serialize(replayJson);
}

int normalizeXDVersion(int version) {
    if (version <= 0)
        return 0;

    if (version < 100)
        return version * 1000;

    if (version < 1000) {
        int major = version / 100;
        int minor = (version / 10) % 10;
        int patch = version % 10;
        return major * 1000 + minor * 100 + patch;
    }

    return version;
}

int parseBotVersion(std::string version) {
    if (!version.empty() && version.front() == 'v')
        version.erase(version.begin());

    auto suffix = version.find_first_of(" \n\r\t-+");
    if (suffix != std::string::npos)
        version = version.substr(0, suffix);

    auto parts = geode::utils::string::split(version, ".");
    if (parts.empty())
        return 0;

    if (parts.size() == 1)
        return normalizeXDVersion(geode::utils::numFromString<int>(parts[0]).unwrapOr(0));

    int major = geode::utils::numFromString<int>(parts[0]).unwrapOr(0);
    int minor = parts.size() > 1 ? geode::utils::numFromString<int>(parts[1]).unwrapOr(0) : 0;
    int patch = parts.size() > 2 ? geode::utils::numFromString<int>(parts[2]).unwrapOr(0) : 0;
    return normalizeXDVersion(major * 1000 + minor * 100 + patch);
}

void normalizeReplayVersion(BotReplay& replay) {
    if (replay.botInfo.name == "xdBot")
        replay.botInfo.version = normalizeXDVersion(replay.botInfo.version);
}

LegacyMacro toLegacy(const BotReplay& replay) {
    LegacyMacro legacy;
    legacy.author = replay.author;
    legacy.description = replay.description;
    legacy.duration = replay.duration;
    legacy.gameVersion = static_cast<float>(replay.gameVersion) / 1000.0f;
    legacy.framerate = replay.framerate;
    legacy.seed = replay.seed;
    legacy.coins = replay.coins;
    legacy.ldm = replay.ldm;
    legacy.botInfo.name = replay.botInfo.name;
    legacy.botInfo.version = replay.getBotVersionString();
    legacy.levelInfo.id = replay.levelInfo.id;
    legacy.levelInfo.name = replay.levelInfo.name;
    legacy.frameFixes = replay.frameFixes;

    for (auto const& input : replay.inputs) {
        legacy.inputs.emplace_back(
            static_cast<int>(input.frame), input.button, input.player2, input.down);
    }

    return legacy;
}

BotReplay fromLegacy(const LegacyMacro& legacy) {
    BotReplay replay;
    replay.author = legacy.author;
    replay.description = legacy.description;
    replay.duration = legacy.duration;
    replay.gameVersion = static_cast<int>(std::round(legacy.gameVersion * 1000.0f));
    replay.framerate = legacy.framerate;
    replay.seed = legacy.seed;
    replay.coins = legacy.coins;
    replay.ldm = legacy.ldm;
    replay.botInfo.name = legacy.botInfo.name;
    replay.botInfo.version = parseBotVersion(legacy.botInfo.version);
    replay.levelInfo.id = legacy.levelInfo.id;
    replay.levelInfo.name = legacy.levelInfo.name;
    replay.frameFixes = legacy.frameFixes;

    for (auto const& input : legacy.inputs) {
        replay.inputs.emplace_back(
            static_cast<uint64_t>(input.frame), static_cast<uint8_t>(input.button), input.player2, input.down);
    }

    normalizeImportedReplay(replay);
    return replay;
}

BotReplay importReplay(std::vector<uint8_t>& data) {
    if (data.size() >= 3 && data[0] == 'G' && data[1] == 'D' && data[2] == 'R') {
        std::span<uint8_t> span(data.data(), data.size());
        auto result = gdr::Replay<BotReplay, ReplayInput>::importData(span);
        if (result.isOk()) {
            auto replay = std::move(result).unwrap();
            normalizeImportedReplay(replay);
            return replay;
        }

        log::warn("GDR2 import failed: {}", result.unwrapErr());
    }

    auto legacy = LegacyMacro::importData(data);
    if (!legacy.inputs.empty())
        return fromLegacy(legacy);

    std::span<uint8_t const> span(data.data(), data.size());
    auto result = gdr::convert<BotReplay, ReplayInput>(span);
    if (result.isOk()) {
        auto replay = std::move(result).unwrap();
        normalizeImportedReplay(replay);
        return replay;
    }

    log::warn("GDR converter import failed: {}", result.unwrapErr());
    log::error("Failed to import macro data in any format");
    return BotReplay();
}

BotReplay importXD(std::filesystem::path path) {
    BotReplay replay;
    replay.author = "N/A";
    replay.description = "N/A";
    replay.gameVersion = static_cast<int>(std::round(static_cast<double>(GEODE_GD_VERSION) * 1000.0));

    auto readResult = geode::utils::file::readString(path);
    if (readResult.isErr()) {
        replay.description = "fail";
        return replay;
    }

    float fpsMultiplier = 1.f;
    auto lines = geode::utils::string::split(readResult.unwrap(), "\n");
    for (auto& line : lines) {
        auto action = geode::utils::string::split(line, "|");
        if (action.empty())
            continue;

        if (action.size() < 4) {
            fpsMultiplier = action[0] == "android"
                                ? 4.f
                                : 240.f / geode::utils::numFromString<float>(action[0]).unwrapOr(240.f);
            continue;
        }

        int frame = static_cast<int>(
            std::round(geode::utils::numFromString<float>(action[0]).unwrapOr(0.f) * fpsMultiplier));
        int button = geode::utils::numFromString<int>(action[2]).unwrapOr(0);
        bool hold = action[1] == "1";
        bool player2 = action[3] == "1";
        bool posOnly = action.size() > 4 && action[4] == "1";

        if (!posOnly) {
            replay.inputs.emplace_back(frame, button, player2, hold);
            continue;
        }

        cocos2d::CCPoint p1Pos = ccp(geode::utils::numFromString<float>(action[5]).unwrapOr(0.f),
                                     geode::utils::numFromString<float>(action[6]).unwrapOr(0.f));
        cocos2d::CCPoint p2Pos = ccp(geode::utils::numFromString<float>(action[11]).unwrapOr(0.f),
                                     geode::utils::numFromString<float>(action[12]).unwrapOr(0.f));
        replay.frameFixes.push_back({frame, {p1Pos, 0.f, false}, {p2Pos, 0.f, false}});
    }

    replay.xdBotMacro = true;
    replay.isLegacy = true;
    return replay;
}
} // namespace gdr_legacy
