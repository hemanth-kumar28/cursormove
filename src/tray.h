/*
 * tray.h — System tray icon management.
 *
 * Provides a clean API for creating/destroying the tray icon,
 * updating its tooltip and icon, and showing the context menu.
 */
#pragma once
#ifndef CM_TRAY_H
#define CM_TRAY_H

#include "util.h"    /* pulls in windows.h */

namespace cm {
namespace tray {

    /* Add the tray icon. Returns true on success.
     * hwnd: the hidden message window that receives WM_TRAYICON.
     * hIcon: the icon to display (can be NULL for default). */
    bool Create(HWND hwnd, HICON hIcon);

    /* Remove the tray icon. Safe to call even if Create() failed. */
    void Destroy(HWND hwnd);

    /* Update the tooltip text shown on hover. */
    void SetTooltip(HWND hwnd, const wchar_t* tip);

    /* Update the icon image. */
    void SetIcon(HWND hwnd, HICON hIcon);

    /* Show the right-click context menu at the cursor position.
     * enabled: whether the "Enabled" item should be checked. */
    void ShowContextMenu(HWND hwnd, bool enabled);

} /* namespace tray */
} /* namespace cm */

#endif /* CM_TRAY_H */
