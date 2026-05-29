/*
 * config.h — JSON config loading, saving, and validation.
 *
 * Config is stored at %APPDATA%\CursorMove\config.json.
 * Missing or invalid fields always fall back to safe defaults.
 */
#pragma once
#ifndef CM_CONFIG_H
#define CM_CONFIG_H

#include "types.h"
#include "hook.h"    /* KeyBindings */
#include <string>

namespace cm {

/* Full application configuration */
struct AppConfig {
    bool          enabled;
    std::string   toggleHotkey;
    std::string   panicHotkey;
    MotionParams  motion;
    hook::KeyBindings keys;
    bool          startWithWindows;
    bool          swallowKeys;
    int           winNormL;
    int           winNormT;
    int           winNormR;
    int           winNormB;
    int           winShowCmd;

    AppConfig()
        : enabled(false)
        , toggleHotkey("Alt+S")
        , panicHotkey("Ctrl+Alt+Esc")
        , motion()
        , keys()
        , startWithWindows(false)
        , swallowKeys(false)
        , winNormL(-1)
        , winNormT(-1)
        , winNormR(-1)
        , winNormB(-1)
        , winShowCmd(1) /* SW_SHOWNORMAL = 1 */
    {}
};

namespace config {

    /* Returns a config with all defaults. */
    AppConfig Default();

    /* Load config from disk. Returns defaults for any missing/invalid fields.
     * Never throws. Never crashes. */
    AppConfig Load();

    /* Save config to disk. Returns true on success. */
    bool Save(const AppConfig& cfg);

    /* Validate and clamp all fields to safe ranges. Modifies in-place. */
    void Validate(AppConfig& cfg);

} /* namespace config */
} /* namespace cm */

#endif /* CM_CONFIG_H */
