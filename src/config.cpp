/*
 * config.cpp — JSON config load/save/validate implementation.
 *
 * Uses nlohmann/json. All file operations are wrapped in try/catch.
 * Invalid or missing fields silently revert to defaults.
 * Config is always saved in normalized form.
 */
#include "config.h"
#include "keys.h"
#include "util.h"

/* Suppress warnings in json.hpp for our old compiler */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#include "json.hpp"
#pragma GCC diagnostic pop

#include <fstream>
#include <string>

using json = nlohmann::json;

namespace cm {
namespace config {

/* ---- Safe JSON field readers (never throw) ---- */

static std::string ReadString(const json& j, const char* key,
                               const std::string& def) {
    try {
        if (j.count(key) && j[key].is_string()) {
            return j[key].get<std::string>();
        }
    } catch (...) {}
    return def;
}

static float ReadFloat(const json& j, const char* key, float def) {
    try {
        if (j.count(key) && j[key].is_number()) {
            return j[key].get<float>();
        }
    } catch (...) {}
    return def;
}

static int ReadInt(const json& j, const char* key, int def) {
    try {
        if (j.count(key) && j[key].is_number()) {
            return j[key].get<int>();
        }
    } catch (...) {}
    return def;
}

static bool ReadBool(const json& j, const char* key, bool def) {
    try {
        if (j.count(key) && j[key].is_boolean()) {
            return j[key].get<bool>();
        }
    } catch (...) {}
    return def;
}

/* ---- Read a VK binding from a JSON key name ---- */
static UINT ReadVk(const json& j, const char* key, UINT def) {
    std::string name = ReadString(j, key, "");
    if (name.empty()) return def;
    UINT vk = keys::VkFromName(name);
    return (vk != 0) ? vk : def;
}

/* ================================================================ */

AppConfig Default() {
    return AppConfig();
}

AppConfig Load() {
    AppConfig cfg;
    std::wstring path = util::GetConfigPath();
    if (path.empty()) return cfg;

    /* Convert wstring path to narrow for ifstream (MinGW 6.3 limitation) */
    std::string narrowPath;
    for (size_t i = 0; i < path.size(); ++i) {
        wchar_t wc = path[i];
        if (wc < 128) {
            narrowPath += static_cast<char>(wc);
        } else {
            narrowPath += '?';  /* non-ASCII path char — rare for AppData */
        }
    }

    try {
        std::ifstream ifs(narrowPath.c_str());
        if (!ifs.is_open()) return cfg;  /* no config yet — use defaults */

        json j;
        ifs >> j;
        ifs.close();

        if (!j.is_object()) return cfg;

        /* Top-level fields */
        cfg.enabled          = ReadBool(j, "enabled", false);
        cfg.toggleHotkey     = ReadString(j, "toggleHotkey", "Alt+S");
        cfg.panicHotkey      = ReadString(j, "panicHotkey", "Ctrl+Alt+Esc");
        cfg.startWithWindows = ReadBool(j, "startWithWindows", false);

        /* Motion parameters */
        cfg.motion.baseSpeed            = ReadFloat(j, "baseSpeed", 180.0f);
        cfg.motion.maxSpeed             = ReadFloat(j, "maxSpeed", 1200.0f);
        cfg.motion.accelTimeMs          = ReadFloat(j, "accelTimeMs", 250.0f);
        cfg.motion.decelTimeMs          = ReadFloat(j, "decelTimeMs", 140.0f);
        cfg.motion.precisionMultiplier  = ReadFloat(j, "precisionMultiplier", 0.35f);
        cfg.motion.smoothingAccel       = ReadFloat(j, "smoothingAccel", 0.15f);
        cfg.motion.smoothingDecel       = ReadFloat(j, "smoothingDecel", 0.25f);
        cfg.motion.tickHz               = ReadInt(j, "tickHz", DEFAULT_TICK_HZ);

        /* Movement keys */
        if (j.count("movementKeys") && j["movementKeys"].is_object()) {
            const json& mk = j["movementKeys"];
            cfg.keys.moveUp    = ReadVk(mk, "up",    'W');
            cfg.keys.moveDown  = ReadVk(mk, "down",  'S');
            cfg.keys.moveLeft  = ReadVk(mk, "left",  'A');
            cfg.keys.moveRight = ReadVk(mk, "right", 'D');
        }

        /* Click keys */
        if (j.count("clickKeys") && j["clickKeys"].is_object()) {
            const json& ck = j["clickKeys"];
            cfg.keys.clickLeft   = ReadVk(ck, "left",   VK_SPACE);
            cfg.keys.clickRight  = ReadVk(ck, "right",  'E');
            cfg.keys.clickMiddle = ReadVk(ck, "middle", 'Q');
        }

        /* Scroll keys */
        if (j.count("scrollKeys") && j["scrollKeys"].is_object()) {
            const json& sk = j["scrollKeys"];
            cfg.keys.scrollUp   = ReadVk(sk, "up",   'R');
            cfg.keys.scrollDown = ReadVk(sk, "down", 'F');
        }

        /* Precision key */
        cfg.keys.precisionToggle = ReadVk(j, "precisionKey", VK_LSHIFT);

    } catch (...) {
        /* Any parse error → return defaults */
        cfg = AppConfig();
    }

    Validate(cfg);
    return cfg;
}

bool Save(const AppConfig& cfg) {
    std::wstring path = util::GetConfigPath();
    if (path.empty()) return false;

    /* Convert wstring to narrow */
    std::string narrowPath;
    for (size_t i = 0; i < path.size(); ++i) {
        wchar_t wc = path[i];
        if (wc < 128) {
            narrowPath += static_cast<char>(wc);
        } else {
            narrowPath += '?';
        }
    }

    try {
        json j;

        j["enabled"]             = cfg.enabled;
        j["toggleHotkey"]        = cfg.toggleHotkey;
        j["panicHotkey"]         = cfg.panicHotkey;
        j["startWithWindows"]    = cfg.startWithWindows;
        j["baseSpeed"]           = cfg.motion.baseSpeed;
        j["maxSpeed"]            = cfg.motion.maxSpeed;
        j["accelTimeMs"]         = cfg.motion.accelTimeMs;
        j["decelTimeMs"]         = cfg.motion.decelTimeMs;
        j["precisionMultiplier"] = cfg.motion.precisionMultiplier;
        j["smoothingAccel"]      = cfg.motion.smoothingAccel;
        j["smoothingDecel"]      = cfg.motion.smoothingDecel;
        j["tickHz"]              = cfg.motion.tickHz;

        j["movementKeys"]["up"]    = keys::NameFromVk(cfg.keys.moveUp);
        j["movementKeys"]["down"]  = keys::NameFromVk(cfg.keys.moveDown);
        j["movementKeys"]["left"]  = keys::NameFromVk(cfg.keys.moveLeft);
        j["movementKeys"]["right"] = keys::NameFromVk(cfg.keys.moveRight);

        j["clickKeys"]["left"]   = keys::NameFromVk(cfg.keys.clickLeft);
        j["clickKeys"]["right"]  = keys::NameFromVk(cfg.keys.clickRight);
        j["clickKeys"]["middle"] = keys::NameFromVk(cfg.keys.clickMiddle);

        j["scrollKeys"]["up"]   = keys::NameFromVk(cfg.keys.scrollUp);
        j["scrollKeys"]["down"] = keys::NameFromVk(cfg.keys.scrollDown);

        j["precisionKey"] = keys::NameFromVk(cfg.keys.precisionToggle);

        std::ofstream ofs(narrowPath.c_str());
        if (!ofs.is_open()) return false;

        ofs << j.dump(2) << std::endl;
        ofs.close();

        return ofs.good() || !ofs.fail();

    } catch (...) {
        return false;
    }
}

void Validate(AppConfig& cfg) {
    /* Clamp motion parameters to safe ranges */
    cfg.motion.baseSpeed           = util::Clamp(cfg.motion.baseSpeed, 10.0f, 2000.0f);
    cfg.motion.maxSpeed            = util::Clamp(cfg.motion.maxSpeed, 50.0f, 5000.0f);
    cfg.motion.accelTimeMs         = util::Clamp(cfg.motion.accelTimeMs, 10.0f, 2000.0f);
    cfg.motion.decelTimeMs         = util::Clamp(cfg.motion.decelTimeMs, 10.0f, 1000.0f);
    cfg.motion.precisionMultiplier = util::Clamp(cfg.motion.precisionMultiplier, 0.05f, 1.0f);
    cfg.motion.smoothingAccel      = util::Clamp(cfg.motion.smoothingAccel, 0.01f, 1.0f);
    cfg.motion.smoothingDecel      = util::Clamp(cfg.motion.smoothingDecel, 0.01f, 1.0f);
    cfg.motion.tickHz              = util::ClampInt(cfg.motion.tickHz, MIN_TICK_HZ, MAX_TICK_HZ);

    /* Ensure maxSpeed >= baseSpeed */
    if (cfg.motion.maxSpeed < cfg.motion.baseSpeed) {
        cfg.motion.maxSpeed = cfg.motion.baseSpeed;
    }

    /* Validate hotkey strings — if empty, use defaults */
    if (cfg.toggleHotkey.empty()) cfg.toggleHotkey = "Alt+S";
    if (cfg.panicHotkey.empty())  cfg.panicHotkey  = "Ctrl+Alt+Esc";

    /* Validate key bindings — if any VK is 0 (invalid), revert to default */
    hook::KeyBindings def;
    if (cfg.keys.moveUp    == 0) cfg.keys.moveUp    = def.moveUp;
    if (cfg.keys.moveDown  == 0) cfg.keys.moveDown  = def.moveDown;
    if (cfg.keys.moveLeft  == 0) cfg.keys.moveLeft  = def.moveLeft;
    if (cfg.keys.moveRight == 0) cfg.keys.moveRight = def.moveRight;
    if (cfg.keys.clickLeft  == 0) cfg.keys.clickLeft  = def.clickLeft;
    if (cfg.keys.clickRight == 0) cfg.keys.clickRight = def.clickRight;
    if (cfg.keys.clickMiddle== 0) cfg.keys.clickMiddle= def.clickMiddle;
    if (cfg.keys.scrollUp   == 0) cfg.keys.scrollUp   = def.scrollUp;
    if (cfg.keys.scrollDown == 0) cfg.keys.scrollDown = def.scrollDown;
    if (cfg.keys.precisionToggle == 0) cfg.keys.precisionToggle = def.precisionToggle;
}

} /* namespace config */
} /* namespace cm */
