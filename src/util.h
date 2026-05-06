/*
 * util.h — Small helper functions (error formatting, paths, clamping).
 */
#pragma once
#ifndef CM_UTIL_H
#define CM_UTIL_H

#ifndef WINVER
#define WINVER 0x0601
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>
#include <string>

namespace cm {
namespace util {

    /* Returns %APPDATA%\CursorMove\  — creates dir if needed.
     * Returns empty string on failure. */
    std::wstring GetConfigDir();

    /* Returns full path to config.json */
    std::wstring GetConfigPath();

    /* Format a Win32 error code into a readable string. */
    std::wstring FormatWinError(DWORD errorCode);

    /* Show a modal error dialog. */
    void ShowError(const wchar_t* title, const wchar_t* message);

    /* Show a modal error dialog with auto-formatted GetLastError(). */
    void ShowLastError(const wchar_t* title);

    /* Clamp a float to [lo, hi]. */
    inline float Clamp(float v, float lo, float hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

    /* Clamp an int to [lo, hi]. */
    inline int ClampInt(int v, int lo, int hi) {
        if (v < lo) return lo;
        if (v > hi) return hi;
        return v;
    }

} /* namespace util */
} /* namespace cm */

#endif /* CM_UTIL_H */
