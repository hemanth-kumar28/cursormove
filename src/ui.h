/*
 * ui.h — Settings window with tabbed interface.
 */
#pragma once
#ifndef CM_UI_H
#define CM_UI_H

#include "util.h"
#include "state.h"
#include "config.h"

namespace cm {
namespace ui {

    /* Initialize the UI module.
     * Must be called before Show(). */
    void Init(HINSTANCE hInst, SharedState* state, AppConfig* config);

    /* Show the settings window (creates it if needed). */
    void Show();

    /* Hide the settings window. */
    void Hide();

    /* Returns true if the settings window is currently visible. */
    bool IsVisible();

    /* Clean up resources. */
    void Destroy();

    /* Returns the settings window handle (or NULL). */
    HWND GetHwnd();

} /* namespace ui */
} /* namespace cm */

#endif /* CM_UI_H */
