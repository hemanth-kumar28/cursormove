/*
 * hotkey.h — Global hotkey registration (toggle + panic).
 */
#pragma once
#ifndef CM_HOTKEY_H
#define CM_HOTKEY_H

#include "util.h"
#include <string>

/* MOD_NOREPEAT may be missing in MinGW 6.3 headers (Win7+ feature) */
#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

namespace cm {
namespace hotkey {

    /* Hotkey IDs */
    enum HotkeyId {
        HOTKEY_TOGGLE = 1,
        HOTKEY_PANIC  = 2
    };

    /* Parse a hotkey string like "Alt+S" or "Ctrl+Alt+Esc"
     * into modifiers and a VK code.
     * Returns true on success. */
    bool ParseHotkey(const std::string& str, UINT& outMod, UINT& outVk);

    /* Register the toggle and panic hotkeys with the given window.
     * Returns true if at least the panic key was registered. */
    bool Register(HWND hwnd, const std::string& toggleStr,
                  const std::string& panicStr);

    /* Unregister all hotkeys. */
    void Unregister(HWND hwnd);

    /* Check if toggle hotkey registration succeeded. */
    bool IsToggleRegistered();

    /* Check if panic hotkey registration succeeded. */
    bool IsPanicRegistered();

} /* namespace hotkey */
} /* namespace cm */

#endif /* CM_HOTKEY_H */
