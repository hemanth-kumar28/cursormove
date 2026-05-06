/*
 * hotkey.cpp — Global hotkey implementation.
 *
 * Parses human-readable hotkey strings and registers them
 * via RegisterHotKey. Handles registration failures gracefully.
 */
#include "hotkey.h"
#include "keys.h"
#include <cctype>
#include <cstring>
#include <vector>

namespace cm {
namespace hotkey {

static bool s_toggleRegistered = false;
static bool s_panicRegistered  = false;

/* ---- Split string by '+' delimiter ---- */
static std::vector<std::string> SplitPlus(const std::string& str) {
    std::vector<std::string> parts;
    std::string current;
    for (size_t i = 0; i < str.size(); ++i) {
        char c = str[i];
        if (c == '+') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
        } else if (c != ' ') {
            current += c;
        }
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

/* ---- Case-insensitive compare ---- */
static bool EqNoCase(const std::string& a, const char* b) {
    if (a.size() != strlen(b)) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (tolower(static_cast<unsigned char>(a[i])) !=
            tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

bool ParseHotkey(const std::string& str, UINT& outMod, UINT& outVk) {
    outMod = 0;
    outVk  = 0;

    std::vector<std::string> parts = SplitPlus(str);
    if (parts.empty()) return false;

    /* Last part is the key, everything before is a modifier */
    for (size_t i = 0; i < parts.size() - 1; ++i) {
        const std::string& p = parts[i];
        if (EqNoCase(p, "Alt"))          outMod |= MOD_ALT;
        else if (EqNoCase(p, "Ctrl") ||
                 EqNoCase(p, "Control"))  outMod |= MOD_CONTROL;
        else if (EqNoCase(p, "Shift"))   outMod |= MOD_SHIFT;
        else if (EqNoCase(p, "Win"))     outMod |= MOD_WIN;
        else return false;  /* Unknown modifier */
    }

    /* The key name */
    const std::string& keyName = parts.back();
    outVk = keys::VkFromName(keyName);

    return outVk != 0;
}

bool Register(HWND hwnd, const std::string& toggleStr,
              const std::string& panicStr)
{
    s_toggleRegistered = false;
    s_panicRegistered  = false;

    /* Register toggle hotkey */
    {
        UINT mod = 0, vk = 0;
        if (ParseHotkey(toggleStr, mod, vk)) {
            /* MOD_NOREPEAT prevents auto-repeat spam (Win7+) */
            if (RegisterHotKey(hwnd, HOTKEY_TOGGLE, mod | MOD_NOREPEAT, vk)) {
                s_toggleRegistered = true;
            }
        }
    }

    /* Register panic hotkey */
    {
        UINT mod = 0, vk = 0;
        if (ParseHotkey(panicStr, mod, vk)) {
            if (RegisterHotKey(hwnd, HOTKEY_PANIC, mod | MOD_NOREPEAT, vk)) {
                s_panicRegistered = true;
            }
        }
    }

    /* We require at least the panic key to succeed */
    return s_panicRegistered;
}

void Unregister(HWND hwnd) {
    UnregisterHotKey(hwnd, HOTKEY_TOGGLE);
    UnregisterHotKey(hwnd, HOTKEY_PANIC);
    s_toggleRegistered = false;
    s_panicRegistered  = false;
}

bool IsToggleRegistered() { return s_toggleRegistered; }
bool IsPanicRegistered()  { return s_panicRegistered; }

} /* namespace hotkey */
} /* namespace cm */
