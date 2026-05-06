/*
 * util.cpp — Implementation of helper functions.
 */
#include "util.h"
#include <shlobj.h>   /* SHGetFolderPathW */

namespace cm {
namespace util {

std::wstring GetConfigDir() {
    wchar_t appData[MAX_PATH] = {0};
    HRESULT hr = SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, 0, appData);
    if (FAILED(hr)) {
        return std::wstring();
    }

    std::wstring dir(appData);
    dir += L"\\CursorMove";

    /* Create directory if it doesn't exist (ignore ERROR_ALREADY_EXISTS) */
    if (!CreateDirectoryW(dir.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            return std::wstring();
        }
    }

    dir += L"\\";
    return dir;
}

std::wstring GetConfigPath() {
    std::wstring dir = GetConfigDir();
    if (dir.empty()) return dir;
    return dir + L"config.json";
}

std::wstring FormatWinError(DWORD errorCode) {
    if (errorCode == 0) {
        return L"No error";
    }

    wchar_t* buf = NULL;
    DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf),
        0,
        NULL
    );

    std::wstring result;
    if (len > 0 && buf != NULL) {
        result.assign(buf, len);
        /* Trim trailing \r\n */
        while (!result.empty() &&
               (result.back() == L'\r' || result.back() == L'\n')) {
            result.pop_back();
        }
    } else {
        wchar_t fallback[64];
        _snwprintf(fallback, 64, L"Unknown error (code %lu)", errorCode);
        result = fallback;
    }

    if (buf) LocalFree(buf);
    return result;
}

void ShowError(const wchar_t* title, const wchar_t* message) {
    MessageBoxW(NULL, message, title,
                MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}

void ShowLastError(const wchar_t* title) {
    DWORD err = GetLastError();
    std::wstring msg = FormatWinError(err);
    ShowError(title, msg.c_str());
}

} /* namespace util */
} /* namespace cm */
