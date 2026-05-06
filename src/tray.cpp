/*
 * tray.cpp — System tray icon implementation.
 *
 * Uses Shell_NotifyIconW for the notification area icon.
 * All WinAPI results are checked; failures are non-fatal.
 */
#include "tray.h"

/* Need objbase.h before shellapi.h for REFIID on MinGW 6.3 */
#include <objbase.h>
#include <shellapi.h>

/* Pull in resource IDs */
#include "../res/resource.h"

/* Array element count — not available in MinGW 6.3 */
#ifndef CM_COUNTOF
#define CM_COUNTOF(arr)  (sizeof(arr) / sizeof((arr)[0]))
#endif

namespace cm {
namespace tray {

/* ---- Internal: fill common NOTIFYICONDATAW fields ---- */
static void FillNid(NOTIFYICONDATAW& nid, HWND hwnd) {
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd   = hwnd;
    nid.uID    = IDI_APPICON;   /* unique icon ID within the app */
}

bool Create(HWND hwnd, HICON hIcon) {
    NOTIFYICONDATAW nid;
    FillNid(nid, hwnd);

    nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.uCallbackMessage = WM_TRAYICON;

    /* Use provided icon, or fall back to system default */
    if (hIcon) {
        nid.hIcon = hIcon;
    } else {
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

    wcsncpy(nid.szTip, L"CursorMove - Disabled", CM_COUNTOF(nid.szTip) - 1);
    nid.szTip[CM_COUNTOF(nid.szTip) - 1] = L'\0';

    BOOL ok = Shell_NotifyIconW(NIM_ADD, &nid);
    if (!ok) {
        /* Retry once — Explorer may not be ready yet (e.g. at login) */
        Sleep(1000);
        ok = Shell_NotifyIconW(NIM_ADD, &nid);
    }

    return ok != FALSE;
}

void Destroy(HWND hwnd) {
    NOTIFYICONDATAW nid;
    FillNid(nid, hwnd);
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void SetTooltip(HWND hwnd, const wchar_t* tip) {
    if (!tip) return;

    NOTIFYICONDATAW nid;
    FillNid(nid, hwnd);
    nid.uFlags = NIF_TIP;
    wcsncpy(nid.szTip, tip, CM_COUNTOF(nid.szTip) - 1);
    nid.szTip[CM_COUNTOF(nid.szTip) - 1] = L'\0';

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void SetIcon(HWND hwnd, HICON hIcon) {
    if (!hIcon) return;

    NOTIFYICONDATAW nid;
    FillNid(nid, hwnd);
    nid.uFlags = NIF_ICON;
    nid.hIcon  = hIcon;

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowContextMenu(HWND hwnd, bool enabled) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    /* Enable / Disable toggle */
    UINT enableFlags = MF_STRING;
    if (enabled) enableFlags |= MF_CHECKED;
    AppendMenuW(hMenu, enableFlags, ID_TRAY_ENABLE, L"Enabled");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Settings (will be enabled in Phase 6) */
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");

    /* Reset defaults */
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_RESET, L"Reset Defaults");

    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Exit */
    AppendMenuW(hMenu, MF_STRING, ID_TRAY_EXIT, L"Exit");

    /* Required Win32 pattern: SetForegroundWindow before TrackPopupMenu,
     * and post WM_NULL after, to ensure the menu dismisses properly. */
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN,
                   pt.x, pt.y, 0, hwnd, NULL);
    PostMessage(hwnd, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

} /* namespace tray */
} /* namespace cm */
