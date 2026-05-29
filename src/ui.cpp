/*
 * ui.cpp — Settings window with retained-mode custom rendering.
 *
 * Zero child HWNDs. One backbuffer. One paint pass.
 * Left sidebar, auto-save, inline key capture, direct-manipulation sliders.
 * Catppuccin Mocha palette. Calm professional utility aesthetic.
 *
 * MinGW 6.3 / C++14 / pure Win32 GDI.
 */
#include "ui.h"
#include "ui_widgets.h"
#include "hook.h"
#include "motion.h"
#include "hotkey.h"
#include "keys.h"

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <commctrl.h>

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#include "../res/resource.h"

#include <cstdio>

/* DwmSetWindowAttribute — loaded dynamically for dark title bar */
typedef HRESULT (WINAPI *PFN_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);
static PFN_DwmSetWindowAttribute pfnDwmSetWindowAttribute = NULL;
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace cm {
namespace ui {

/* ---- Window dimensions ---- */
static const int WIN_W = 560;
static const int WIN_H = 500;

/* ---- Module state ---- */
static HINSTANCE   s_hInst    = NULL;
static SharedState* s_state   = NULL;
static AppConfig*  s_config   = NULL;
static HWND        s_hwnd     = NULL;
static const wchar_t* s_wndClass = L"CursorMoveSettingsV2";

/* ---- Tab / interaction state ---- */
static int  s_activeTab   = 0;
static int  s_hoverTab    = -1;
static int  s_hoverWidget = -1;
static int  s_focusWidget = -1;
static bool s_dragging    = false;  /* slider drag in progress */
static int  s_dragWidget  = -1;

/* ---- Key capture state ---- */
static int    s_captureTarget  = -1;   /* widget index being remapped */
static DWORD  s_captureStart   = 0;
static bool   s_captureBlink   = false;
static UINT   s_captureOrigVk  = 0;    /* original VK to restore on cancel */

/* ---- Toast state ---- */
static bool   s_toastVisible   = false;
static DWORD  s_toastStart     = 0;
static int    s_toastFade      = 0;    /* 0 = full, 1-4 = fading, 5 = hidden */

/* ---- Progressive disclosure ---- */
static bool s_advancedExpanded = false;

/* ---- Timer IDs ---- */
enum {
    TIMER_DIAG       = 1,   /* 500ms: diagnostics refresh */
    TIMER_TOAST      = 3,   /* 100ms: toast fade steps */
    TIMER_CAPTURE_BLINK = 4,  /* 300ms: capture border pulse */
    TIMER_CAPTURE_TIMEOUT = 5 /* 1000ms: capture countdown */
};

/* ---- Panels ---- */
static Panel s_panels[5];

/* ---- Sidebar rect (computed once on WM_CREATE) ---- */
static RECT s_sidebarRect;
static RECT s_contentRect;

/* ---- Unique widget IDs ---- */
enum WidgetId {
    /* General */
    WID_GEN_ENABLED = 100,
    WID_GEN_SWALLOW,
    WID_GEN_STARTUP,
    WID_GEN_RESET,

    /* Controls */
    WID_KEY_MOVE_UP = 200,
    WID_KEY_MOVE_DOWN,
    WID_KEY_MOVE_LEFT,
    WID_KEY_MOVE_RIGHT,
    WID_KEY_CLICK_LEFT,
    WID_KEY_CLICK_RIGHT,
    WID_KEY_CLICK_MIDDLE,
    WID_KEY_SCROLL_UP,
    WID_KEY_SCROLL_DOWN,
    WID_KEY_PRECISION,

    /* Motion */
    WID_MOT_BASE = 300,
    WID_MOT_MAX,
    WID_MOT_ACCEL,
    WID_MOT_DECEL,
    WID_MOT_ADVANCED_LINK,
    WID_MOT_PREC,
    WID_MOT_SMOOTH,
    WID_MOT_TICK,
    WID_MOT_SCROLL,
    WID_MOT_RESET,

    /* Diagnostics */
    WID_DIAG_HOOK = 400,
    WID_DIAG_MOTION,
    WID_DIAG_TICK,
    WID_DIAG_CPU,
    WID_DIAG_ERR,
    WID_DIAG_MODE,
};

/* ---- Link action IDs ---- */
enum LinkAction {
    LINK_RESET_ALL = 1,
    LINK_TOGGLE_ADVANCED = 2,
    LINK_RESET_MOTION = 3,
};

/* ---- Intermediate int values for sliders ---- */
/* Sliders need int* pointers. We use these and sync to/from config. */
static int s_baseSpeed, s_maxSpeed, s_accelTime, s_decelTime;
static int s_precPct, s_smoothPct, s_tickHz, s_scrollSpeed;

static void SyncSlidersFromConfig() {
    if (!s_config) return;
    s_baseSpeed  = (int)s_config->motion.baseSpeed;
    s_maxSpeed   = (int)s_config->motion.maxSpeed;
    s_accelTime  = (int)s_config->motion.accelTimeMs;
    s_decelTime  = (int)s_config->motion.decelTimeMs;
    s_precPct    = (int)(s_config->motion.precisionMultiplier * 100.0f);
    s_smoothPct  = (int)(s_config->motion.smoothingAccel * 100.0f);
    s_tickHz     = s_config->motion.tickHz;
    s_scrollSpeed = (int)s_config->motion.scrollSpeed;
}

static void SyncSlidersToConfig() {
    if (!s_config) return;
    s_config->motion.baseSpeed           = (float)s_baseSpeed;
    s_config->motion.maxSpeed            = (float)s_maxSpeed;
    s_config->motion.accelTimeMs         = (float)s_accelTime;
    s_config->motion.decelTimeMs         = (float)s_decelTime;
    s_config->motion.precisionMultiplier = s_precPct / 100.0f;
    s_config->motion.smoothingAccel      = s_smoothPct / 100.0f;
    s_config->motion.smoothingDecel      = s_smoothPct / 100.0f;
    s_config->motion.tickHz              = s_tickHz;
    s_config->motion.scrollSpeed         = (float)s_scrollSpeed;
}

/* ---- Auto-save + toast ---- */
static void AutoSave() {
    if (!s_config) return;
    config::Validate(*s_config);
    config::Save(*s_config);
    motion::UpdateParams(s_config->motion);

    /* Show toast */
    s_toastVisible = true;
    s_toastStart = GetTickCount();
    s_toastFade = 0;
    SetTimer(s_hwnd, TIMER_TOAST, 100, NULL);
    if (s_hwnd) InvalidateRect(s_hwnd, NULL, FALSE);
}

/* ---- Update key badge display text ---- */
static void UpdateKeyBadgeText(Widget& w) {
    if (w.vkVal) {
        std::string name = keys::NameFromVk(*w.vkVal);
        for (int i = 0; i < 31 && name[i]; ++i) {
            w.valueText[i] = (wchar_t)name[i];
            w.valueText[i + 1] = L'\0';
        }
    }
}

/* ================================================================
 * Build panels
 * ================================================================ */
static void BuildGeneralPanel() {
    Panel& p = s_panels[0];
    p.count = 0;

    p.Add(WidgetType::SectionHeader, 0, L"Status");
    p.Add(WidgetType::Separator, 0, L"");

    Widget* wEnabled = p.Add(WidgetType::Toggle, WID_GEN_ENABLED, L"Enabled");
    if (wEnabled && s_state) {
        static bool s_enabledLocal = false;
        s_enabledLocal = s_state->enabled.load(std::memory_order_relaxed);
        wEnabled->boolVal = &s_enabledLocal;
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.SM;

    p.Add(WidgetType::SectionHeader, 0, L"Hotkeys");
    p.Add(WidgetType::Separator, 0, L"");

    Widget* wToggle = p.Add(WidgetType::Label, 0, L"Toggle");
    /* Show the actual hotkey string beside it */
    if (wToggle) {
        _snwprintf(wToggle->valueText, 31, L"Alt + S");
    }

    Widget* wPanic = p.Add(WidgetType::Label, 0, L"Panic");
    if (wPanic) {
        _snwprintf(wPanic->valueText, 31, L"Ctrl + Alt + Esc");
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.SM;

    p.Add(WidgetType::SectionHeader, 0, L"Options");
    p.Add(WidgetType::Separator, 0, L"");

    Widget* wSwallow = p.Add(WidgetType::Toggle, WID_GEN_SWALLOW,
                              L"Swallow keys");
    if (wSwallow && s_config) wSwallow->boolVal = &s_config->swallowKeys;

    p.Add(WidgetType::MetaLabel, 0, L"Block bound keys from reaching other apps");

    Widget* wStartup = p.Add(WidgetType::Toggle, WID_GEN_STARTUP,
                              L"Start with Windows");
    if (wStartup && s_config) wStartup->boolVal = &s_config->startWithWindows;

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.LG;

    Widget* wReset = p.Add(WidgetType::LinkButton, WID_GEN_RESET,
                            L"Reset all settings");
    if (wReset) wReset->linkAction = LINK_RESET_ALL;
}

static void BuildControlsPanel() {
    Panel& p = s_panels[1];
    p.count = 0;

    p.Add(WidgetType::SectionHeader, 0, L"Key Bindings");
    p.Add(WidgetType::Separator, 0, L"");

    struct KeyRow {
        const wchar_t* label;
        int id;
        UINT* vk;
    };

    KeyRow rows[] = {
        {L"Move Up",       WID_KEY_MOVE_UP,     &s_config->keys.moveUp},
        {L"Move Down",     WID_KEY_MOVE_DOWN,    &s_config->keys.moveDown},
        {L"Move Left",     WID_KEY_MOVE_LEFT,    &s_config->keys.moveLeft},
        {L"Move Right",    WID_KEY_MOVE_RIGHT,   &s_config->keys.moveRight},
    };
    for (int i = 0; i < 4; ++i) {
        Widget* w = p.Add(WidgetType::KeyBadge, rows[i].id, rows[i].label);
        if (w) {
            w->vkVal = rows[i].vk;
            UpdateKeyBadgeText(*w);
        }
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.MD;

    KeyRow clickRows[] = {
        {L"Click Left",    WID_KEY_CLICK_LEFT,   &s_config->keys.clickLeft},
        {L"Click Right",   WID_KEY_CLICK_RIGHT,  &s_config->keys.clickRight},
        {L"Click Middle",  WID_KEY_CLICK_MIDDLE,  &s_config->keys.clickMiddle},
    };
    for (int i = 0; i < 3; ++i) {
        Widget* w = p.Add(WidgetType::KeyBadge, clickRows[i].id, clickRows[i].label);
        if (w) {
            w->vkVal = clickRows[i].vk;
            UpdateKeyBadgeText(*w);
        }
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.MD;

    KeyRow scrollRows[] = {
        {L"Scroll Up",     WID_KEY_SCROLL_UP,    &s_config->keys.scrollUp},
        {L"Scroll Down",   WID_KEY_SCROLL_DOWN,  &s_config->keys.scrollDown},
    };
    for (int i = 0; i < 2; ++i) {
        Widget* w = p.Add(WidgetType::KeyBadge, scrollRows[i].id, scrollRows[i].label);
        if (w) {
            w->vkVal = scrollRows[i].vk;
            UpdateKeyBadgeText(*w);
        }
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.MD;

    Widget* wPrec = p.Add(WidgetType::KeyBadge, WID_KEY_PRECISION, L"Precision Hold");
    if (wPrec) {
        wPrec->vkVal = &s_config->keys.precisionToggle;
        UpdateKeyBadgeText(*wPrec);
    }
}

static void BuildMotionPanel() {
    Panel& p = s_panels[2];
    p.count = 0;

    SyncSlidersFromConfig();

    p.Add(WidgetType::SectionHeader, 0, L"Cursor Speed");
    p.Add(WidgetType::Separator, 0, L"");

    Widget* wBase = p.Add(WidgetType::Slider, WID_MOT_BASE, L"Base speed");
    if (wBase) {
        wBase->intVal = &s_baseSpeed;
        wBase->sliderMin = 50; wBase->sliderMax = 500;
        wBase->sliderDefault = 180; wBase->sliderUnit = L"px/s";
    }

    Widget* wMax = p.Add(WidgetType::Slider, WID_MOT_MAX, L"Max speed");
    if (wMax) {
        wMax->intVal = &s_maxSpeed;
        wMax->sliderMin = 200; wMax->sliderMax = 3000;
        wMax->sliderDefault = 1200; wMax->sliderUnit = L"px/s";
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.SM;
    p.Add(WidgetType::SectionHeader, 0, L"Acceleration");
    p.Add(WidgetType::Separator, 0, L"");

    Widget* wAccel = p.Add(WidgetType::Slider, WID_MOT_ACCEL, L"Accel time");
    if (wAccel) {
        wAccel->intVal = &s_accelTime;
        wAccel->sliderMin = 50; wAccel->sliderMax = 1000;
        wAccel->sliderDefault = 250; wAccel->sliderUnit = L"ms";
    }

    Widget* wDecel = p.Add(WidgetType::Slider, WID_MOT_DECEL, L"Decel time");
    if (wDecel) {
        wDecel->intVal = &s_decelTime;
        wDecel->sliderMin = 50; wDecel->sliderMax = 500;
        wDecel->sliderDefault = 140; wDecel->sliderUnit = L"ms";
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.SM;

    /* Advanced toggle */
    Widget* wAdv = p.Add(WidgetType::LinkButton, WID_MOT_ADVANCED_LINK, L"");
    if (wAdv) {
        wAdv->linkAction = LINK_TOGGLE_ADVANCED;
        wcscpy(wAdv->label, s_advancedExpanded ? L"Advanced \x25BE" : L"Advanced \x25B8");
    }

    /* Advanced widgets — visibility controlled by s_advancedExpanded */
    Widget* wPrec = p.Add(WidgetType::Slider, WID_MOT_PREC, L"Precision %");
    if (wPrec) {
        wPrec->intVal = &s_precPct;
        wPrec->sliderMin = 5; wPrec->sliderMax = 100;
        wPrec->sliderDefault = 35; wPrec->sliderUnit = L"%";
        wPrec->visible = s_advancedExpanded;
    }

    Widget* wSmooth = p.Add(WidgetType::Slider, WID_MOT_SMOOTH, L"Smoothing %");
    if (wSmooth) {
        wSmooth->intVal = &s_smoothPct;
        wSmooth->sliderMin = 1; wSmooth->sliderMax = 100;
        wSmooth->sliderDefault = 15; wSmooth->sliderUnit = L"%";
        wSmooth->visible = s_advancedExpanded;
    }

    Widget* wTick = p.Add(WidgetType::Slider, WID_MOT_TICK, L"Tick rate");
    if (wTick) {
        wTick->intVal = &s_tickHz;
        wTick->sliderMin = 60; wTick->sliderMax = 120;
        wTick->sliderDefault = 90; wTick->sliderUnit = L"Hz";
        wTick->visible = s_advancedExpanded;
    }

    Widget* wScroll = p.Add(WidgetType::Slider, WID_MOT_SCROLL, L"Scroll speed");
    if (wScroll) {
        wScroll->intVal = &s_scrollSpeed;
        wScroll->sliderMin = 50; wScroll->sliderMax = 2000;
        wScroll->sliderDefault = 600; wScroll->sliderUnit = L"u/s";
        wScroll->visible = s_advancedExpanded;
    }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.SM;

    Widget* wReset = p.Add(WidgetType::LinkButton, WID_MOT_RESET, L"Reset to defaults");
    if (wReset) wReset->linkAction = LINK_RESET_MOTION;
}

static void BuildDiagnosticsPanel() {
    Panel& p = s_panels[3];
    p.count = 0;

    p.Add(WidgetType::SectionHeader, 0, L"System Status");
    p.Add(WidgetType::Separator, 0, L"");

    Widget* w;
    w = p.Add(WidgetType::StatusRow, WID_DIAG_HOOK, L"Hook");
    if (w) { w->statusColor = 1; wcscpy(w->valueText, L"Active"); }

    w = p.Add(WidgetType::StatusRow, WID_DIAG_MOTION, L"Motion");
    if (w) { w->statusColor = 1; wcscpy(w->valueText, L"Running"); }

    w = p.Add(WidgetType::StatusRow, WID_DIAG_TICK, L"Tick rate");
    if (w) { w->statusColor = 0; wcscpy(w->valueText, L"90 Hz"); }

    w = p.Add(WidgetType::StatusRow, WID_DIAG_CPU, L"CPU");
    if (w) { w->statusColor = 0; wcscpy(w->valueText, L"0%"); }

    w = p.Add(WidgetType::StatusRow, WID_DIAG_ERR, L"Last error");
    if (w) { w->statusColor = 0; wcscpy(w->valueText, L"None"); }

    w = p.Add(WidgetType::StatusRow, WID_DIAG_MODE, L"Mode");
    if (w) { w->statusColor = 0; wcscpy(w->valueText, L"Normal"); }

    p.Add(WidgetType::Spacer, 0, L"")->spacerH = g_theme.spacing.LG;
    p.Add(WidgetType::MetaLabel, 0, L"Refreshes every 500ms");
}

static void BuildAboutPanel() {
    Panel& p = s_panels[4];
    p.count = 0;

    p.Add(WidgetType::SectionHeader, 0, L"CursorMove");
    p.Add(WidgetType::Separator, 0, L"");

    p.Add(WidgetType::Label, 0, L"Version 1.0");
    p.Add(WidgetType::MetaLabel, 0, L"Keyboard-driven cursor control");
    p.Add(WidgetType::MetaLabel, 0, L"for Windows");
}

static void BuildAllPanels() {
    BuildGeneralPanel();
    BuildControlsPanel();
    BuildMotionPanel();
    BuildDiagnosticsPanel();
    BuildAboutPanel();
}

/* ================================================================
 * Diagnostics refresh
 * ================================================================ */
static void UpdateDiagnostics() {
    if (!s_state) return;
    Panel& p = s_panels[3];
    wchar_t buf[32];

    for (int i = 0; i < p.count; ++i) {
        Widget& w = p.widgets[i];
        switch (w.id) {
        case WID_DIAG_HOOK: {
            bool alive = s_state->hookAlive.load(std::memory_order_relaxed);
            wcscpy(w.valueText, alive ? L"Active" : L"Inactive");
            w.statusColor = alive ? 1 : 2;
            break;
        }
        case WID_DIAG_MOTION: {
            bool alive = s_state->motionAlive.load(std::memory_order_relaxed);
            wcscpy(w.valueText, alive ? L"Running" : L"Stopped");
            w.statusColor = alive ? 1 : 2;
            break;
        }
        case WID_DIAG_TICK:
            _snwprintf(buf, 32, L"%d Hz",
                s_state->currentTickHz.load(std::memory_order_relaxed));
            wcscpy(w.valueText, buf);
            break;
        case WID_DIAG_CPU:
            _snwprintf(buf, 32, L"%d%%",
                s_state->cpuLoadPercent.load(std::memory_order_relaxed));
            wcscpy(w.valueText, buf);
            break;
        case WID_DIAG_ERR: {
            DWORD err = s_state->lastError.load(std::memory_order_relaxed);
            if (err == 0) wcscpy(w.valueText, L"None");
            else { _snwprintf(buf, 32, L"%lu", (unsigned long)err); wcscpy(w.valueText, buf); }
            break;
        }
        case WID_DIAG_MODE: {
            bool en = s_state->enabled.load(std::memory_order_relaxed);
            bool pr = s_state->precisionMode.load(std::memory_order_relaxed);
            wcscpy(w.valueText, !en ? L"Disabled" : pr ? L"Precision" : L"Normal");
            break;
        }
        }
    }
}

/* Sync the General panel's "Enabled" toggle from live state */
static void SyncEnabledToggle() {
    Panel& p = s_panels[0];
    for (int i = 0; i < p.count; ++i) {
        if (p.widgets[i].id == WID_GEN_ENABLED && p.widgets[i].boolVal) {
            *p.widgets[i].boolVal = s_state->enabled.load(std::memory_order_relaxed);
        }
    }
}

/* FindWidget removed — use direct iteration when needed */

/* ================================================================
 * Key capture helpers
 * ================================================================ */
static void StartCapture(int widgetIdx) {
    Panel& p = s_panels[1];
    if (widgetIdx < 0 || widgetIdx >= p.count) return;
    Widget& w = p.widgets[widgetIdx];
    if (w.type != WidgetType::KeyBadge) return;

    s_captureTarget = widgetIdx;
    s_captureOrigVk = w.vkVal ? *w.vkVal : 0;
    s_captureStart = GetTickCount();
    s_captureBlink = true;

    /* Transform to capture widget */
    w.type = WidgetType::KeyCapture;
    wcscpy(w.valueText, L"3s");

    /* Start hook capture */
    hook::BeginCapture(s_hwnd);

    /* Start blink and countdown timers */
    SetTimer(s_hwnd, TIMER_CAPTURE_BLINK, 300, NULL);
    SetTimer(s_hwnd, TIMER_CAPTURE_TIMEOUT, 1000, NULL);
    InvalidateRect(s_hwnd, NULL, FALSE);
}

static void EndCapture(bool success, UINT newVk = 0) {
    if (s_captureTarget < 0) return;
    Panel& p = s_panels[1];
    Widget& w = p.widgets[s_captureTarget];

    /* Kill timers */
    KillTimer(s_hwnd, TIMER_CAPTURE_BLINK);
    KillTimer(s_hwnd, TIMER_CAPTURE_TIMEOUT);
    hook::EndCapture();

    if (success && newVk != 0 && w.vkVal) {
        *w.vkVal = newVk;
        hook::UpdateBindings(s_config->keys);
        config::Save(*s_config);

        /* Show toast */
        s_toastVisible = true;
        s_toastStart = GetTickCount();
        s_toastFade = 0;
        SetTimer(s_hwnd, TIMER_TOAST, 100, NULL);
    } else if (w.vkVal) {
        /* Restore original */
        *w.vkVal = s_captureOrigVk;
    }

    /* Restore to key badge */
    w.type = WidgetType::KeyBadge;
    UpdateKeyBadgeText(w);

    s_captureTarget = -1;
    InvalidateRect(s_hwnd, NULL, FALSE);
}

/* ================================================================
 * Handle link button actions
 * ================================================================ */
static void HandleLinkAction(int action) {
    switch (action) {
    case LINK_RESET_ALL: {
        *s_config = config::Default();
        config::Validate(*s_config);
        hook::UpdateBindings(s_config->keys);
        hook::SetSwallowKeys(s_config->swallowKeys);
        SyncSlidersFromConfig();
        BuildAllPanels();
        LayoutPanel(s_panels[s_activeTab], s_contentRect);
        AutoSave();
        break;
    }
    case LINK_TOGGLE_ADVANCED:
        s_advancedExpanded = !s_advancedExpanded;
        BuildMotionPanel();
        LayoutPanel(s_panels[2], s_contentRect);
        InvalidateRect(s_hwnd, NULL, FALSE);
        break;

    case LINK_RESET_MOTION: {
        MotionParams def;
        s_config->motion = def;
        SyncSlidersFromConfig();
        BuildMotionPanel();
        LayoutPanel(s_panels[2], s_contentRect);
        AutoSave();
        break;
    }
    }
}

/* ---- DPI & Scroll state ---- */
static int  s_scrollY = 0;
static int  s_maxScroll = 0;
static bool s_scrollDragging = false;
static bool s_scrollHover = false;
static int  s_wheelAccumulator = 0;
static int  s_lastMouseY = 0;

typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
static PFN_GetDpiForWindow pfnGetDpiForWindow = NULL;

typedef BOOL (WINAPI *PFN_AdjustWindowRectExForDpi)(LPRECT, DWORD, BOOL, DWORD, UINT);
static PFN_AdjustWindowRectExForDpi pfnAdjustWindowRectExForDpi = NULL;

/* ================================================================
 * Settings window procedure
 * ================================================================ */
static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam) {
    switch (msg) {

    /* ---- Window creation ---- */
    case WM_CREATE: {
        s_hwnd = hwnd;

        /* Get initial DPI and initialize theme */
        int dpi = 96;
        if (pfnGetDpiForWindow) {
            dpi = pfnGetDpiForWindow(hwnd);
        } else {
            HDC hdc = GetDC(hwnd);
            dpi = GetDeviceCaps(hdc, LOGPIXELSX);
            ReleaseDC(hwnd, hdc);
        }
        g_theme.Init(dpi);

        /* Compute layout regions */
        RECT clientRc;
        GetClientRect(hwnd, &clientRc);
        s_sidebarRect = { 0, 0, g_theme.spacing.SidebarW, clientRc.bottom };
        s_contentRect = { g_theme.spacing.SidebarW, 0, clientRc.right, clientRc.bottom };

        /* Build all panels */
        BuildAllPanels();
        LayoutPanel(s_panels[s_activeTab], s_contentRect);

        /* Start diagnostics timer */
        SetTimer(hwnd, TIMER_DIAG, 500, NULL);

        return 0;
    }

    case WM_DPICHANGED: {
        int newDpi = HIWORD(wParam);
        g_theme.Init(newDpi);

        RECT* suggested = (RECT*)lParam;
        SetWindowPos(hwnd, NULL,
            suggested->left, suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);

        return 0;
    }

    case WM_SIZE: {
        int cw = (short)LOWORD(lParam);
        int ch = (short)HIWORD(lParam);

        s_sidebarRect = { 0, 0, g_theme.spacing.SidebarW, ch };
        s_contentRect = { g_theme.spacing.SidebarW, 0, cw, ch };

        Panel& panel = s_panels[s_activeTab];
        LayoutPanel(panel, s_contentRect);

        int totalContentHeight = 0;
        if (panel.count > 0) {
            totalContentHeight = panel.widgets[panel.count - 1].bounds.bottom + g_theme.spacing.LG;
        }

        s_maxScroll = totalContentHeight - ch;
        if (s_maxScroll < 0) s_maxScroll = 0;

        if (s_scrollY > s_maxScroll) s_scrollY = s_maxScroll;
        return 0;
    }

    /* ---- Paint ---- */
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        /* Double buffer */
        RECT clientRc;
        GetClientRect(hwnd, &clientRc);
        int cw = clientRc.right - clientRc.left;
        int ch = clientRc.bottom - clientRc.top;

        HDC memDC = CreateCompatibleDC(hdc);
        HBITMAP memBmp = CreateCompatibleBitmap(hdc, cw, ch);
        HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, memBmp);

        /* Fill backgrounds */
        FillRect(memDC, &s_contentRect, g_theme.brBase);

        /* Paint sidebar (not scrolled) */
        PaintSidebar(memDC, s_sidebarRect, s_activeTab, s_hoverTab, g_theme);

        /* Apply clipping to content area to avoid drawing over sidebar or outside */
        HRGN hClip = CreateRectRgnIndirect(&s_contentRect);
        SelectClipRgn(memDC, hClip);
        DeleteObject(hClip); // CRITICAL: Free immediately

        /* Apply viewport translation for scrolling */
        SetViewportOrgEx(memDC, 0, -s_scrollY, NULL);

        /* Paint active panel widgets */
        Panel& panel = s_panels[s_activeTab];
        for (int i = 0; i < panel.count; ++i) {
            bool focused = (i == s_focusWidget);
            PaintWidget(memDC, panel.widgets[i], g_theme,
                        focused, s_captureBlink ? 1 : 0);
        }

        /* Reset viewport and clipping */
        SetViewportOrgEx(memDC, 0, 0, NULL);
        SelectClipRgn(memDC, NULL);

        /* Draw custom scrollbar */
        if (s_maxScroll > 0) {
            int sbWidth = (int)(10 * g_theme.scale);
            int sbRight = clientRc.right - (int)(4 * g_theme.scale);
            int sbLeft = sbRight - sbWidth;
            int sbTop = s_contentRect.top + (int)(4 * g_theme.scale);
            int sbBottom = s_contentRect.bottom - (int)(4 * g_theme.scale);
            int sbHeight = sbBottom - sbTop;

            /* Thumb height proportional to visible area */
            float visibleRatio = (float)ch / (float)(ch + s_maxScroll);
            if (visibleRatio > 1.0f) visibleRatio = 1.0f;
            int thumbHeight = (int)(sbHeight * visibleRatio);
            if (thumbHeight < (int)(20 * g_theme.scale)) thumbHeight = (int)(20 * g_theme.scale);

            /* Thumb position */
            float scrollRatio = (float)s_scrollY / (float)s_maxScroll;
            int thumbTop = sbTop + (int)(scrollRatio * (sbHeight - thumbHeight));

            RECT sbRc = { sbLeft, sbTop, sbRight, sbBottom };
            FillRoundRect(memDC, sbRc, (int)(4 * g_theme.scale), g_theme.brSurface0);

            RECT thumbRc = { sbLeft + (int)(2 * g_theme.scale), thumbTop + (int)(2 * g_theme.scale),
                             sbRight - (int)(2 * g_theme.scale), thumbTop + thumbHeight - (int)(2 * g_theme.scale) };
            
            HBRUSH thumbBr = g_theme.brSurface2;
            if (s_scrollDragging) thumbBr = g_theme.brAccent;
            else if (s_scrollHover) thumbBr = g_theme.brText;
            
            FillRoundRect(memDC, thumbRc, (int)(3 * g_theme.scale), thumbBr);
        }

        /* Paint toast if visible */
        if (s_toastVisible && s_toastFade < 5) {
            PaintToast(memDC, s_contentRect, g_theme, s_toastFade);
        }

        /* Blit to screen */
        BitBlt(hdc, 0, 0, cw, ch, memDC, 0, 0, SRCCOPY);

        SelectObject(memDC, oldBmp);
        DeleteObject(memBmp);
        DeleteDC(memDC);

        EndPaint(hwnd, &ps);
        return 0;
    }

    /* ---- Erase background (handled in WM_PAINT) ---- */
    case WM_ERASEBKGND:
        return 1;

    /* ---- Mouse move ---- */
    case WM_MOUSEMOVE: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);
        
        /* Scroll dragging */
        if (s_scrollDragging) {
            int deltaY = my - s_lastMouseY;
            s_lastMouseY = my;

            RECT clientRc;
            GetClientRect(hwnd, &clientRc);
            int ch = clientRc.bottom - clientRc.top;

            int sbHeight = ch - (int)(8 * g_theme.scale);
            float visibleRatio = (float)ch / (float)(ch + s_maxScroll);
            if (visibleRatio > 1.0f) visibleRatio = 1.0f;
            int thumbHeight = (int)(sbHeight * visibleRatio);
            if (thumbHeight < (int)(20 * g_theme.scale)) thumbHeight = (int)(20 * g_theme.scale);

            float scrollRatioPerPixel = (float)s_maxScroll / (float)(sbHeight - thumbHeight);
            
            s_scrollY += (int)(deltaY * scrollRatioPerPixel);
            if (s_scrollY < 0) s_scrollY = 0;
            if (s_scrollY > s_maxScroll) s_scrollY = s_maxScroll;

            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        
        s_lastMouseY = my;

        /* Track mouse leave */
        TRACKMOUSEEVENT tme;
        tme.cbSize = sizeof(tme);
        tme.dwFlags = TME_LEAVE;
        tme.hwndTrack = hwnd;
        tme.dwHoverTime = 0;
        TrackMouseEvent(&tme);

        /* Sidebar hover */
        int newHoverTab = HitTestSidebar(s_sidebarRect, mx, my);
        if (newHoverTab != s_hoverTab) {
            s_hoverTab = newHoverTab;
            InvalidateRect(hwnd, &s_sidebarRect, FALSE);
        }

        /* Content hover */
        if (mx >= s_contentRect.left) {
            /* Scrollbar hover hit test */
            RECT clientRc;
            GetClientRect(hwnd, &clientRc);
            int sbWidth = (int)(14 * g_theme.scale);
            int sbLeft = clientRc.right - sbWidth;
            
            bool newScrollHover = (mx >= sbLeft && s_maxScroll > 0);
            if (newScrollHover != s_scrollHover) {
                s_scrollHover = newScrollHover;
                InvalidateRect(hwnd, NULL, FALSE);
            }

            Panel& panel = s_panels[s_activeTab];

            /* Slider drag */
            if (s_dragging && s_dragWidget >= 0 && s_dragWidget < panel.count) {
                Widget& w = panel.widgets[s_dragWidget];
                if (w.type == WidgetType::Slider && w.intVal) {
                    *w.intVal = SliderValueFromX(w, mx);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
                return 0;
            }

            int newHover = HitTestPanel(panel, mx, my + s_scrollY);
            if (newHover != s_hoverWidget) {
                /* Clear old hover */
                if (s_hoverWidget >= 0 && s_hoverWidget < panel.count) {
                    panel.widgets[s_hoverWidget].state = WState::Normal;
                }
                s_hoverWidget = newHover;
                if (s_hoverWidget >= 0) {
                    panel.widgets[s_hoverWidget].state = WState::Hover;
                }
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    /* ---- Mouse leave ---- */
    case WM_MOUSELEAVE:
        if (s_hoverTab >= 0) {
            s_hoverTab = -1;
            InvalidateRect(hwnd, &s_sidebarRect, FALSE);
        }
        if (s_hoverWidget >= 0) {
            Panel& panel = s_panels[s_activeTab];
            if (s_hoverWidget < panel.count) {
                panel.widgets[s_hoverWidget].state = WState::Normal;
            }
            s_hoverWidget = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        if (s_scrollHover) {
            s_scrollHover = false;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    /* ---- Left button down ---- */
    case WM_LBUTTONDOWN: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);

        /* Sidebar click */
        int tabIdx = HitTestSidebar(s_sidebarRect, mx, my);
        if (tabIdx >= 0 && tabIdx != s_activeTab) {
            /* Cancel any active capture */
            if (s_captureTarget >= 0) EndCapture(false);

            s_activeTab = tabIdx;
            s_hoverWidget = -1;
            s_focusWidget = -1;
            s_scrollY = 0;
            SyncEnabledToggle();
            LayoutPanel(s_panels[s_activeTab], s_contentRect);
            
            RECT clientRc;
            GetClientRect(hwnd, &clientRc);
            SendMessage(hwnd, WM_SIZE, 0, MAKELPARAM(clientRc.right, clientRc.bottom));
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Content click */
        if (mx >= s_contentRect.left) {
            /* Check scrollbar click */
            RECT clientRc;
            GetClientRect(hwnd, &clientRc);
            int ch = clientRc.bottom - clientRc.top;
            int sbWidth = (int)(14 * g_theme.scale);
            int sbLeft = clientRc.right - sbWidth;
            
            if (mx >= sbLeft && s_maxScroll > 0) {
                int sbHeight = ch - (int)(8 * g_theme.scale);
                float visibleRatio = (float)ch / (float)(ch + s_maxScroll);
                if (visibleRatio > 1.0f) visibleRatio = 1.0f;
                int thumbHeight = (int)(sbHeight * visibleRatio);
                if (thumbHeight < (int)(20 * g_theme.scale)) thumbHeight = (int)(20 * g_theme.scale);

                float scrollRatio = (float)s_scrollY / (float)s_maxScroll;
                int thumbTop = (int)(4 * g_theme.scale) + (int)(scrollRatio * (sbHeight - thumbHeight));
                int thumbBottom = thumbTop + thumbHeight;

                if (my >= thumbTop && my <= thumbBottom) {
                    /* Clicked on thumb */
                    s_scrollDragging = true;
                    s_lastMouseY = my;
                    SetCapture(hwnd);
                } else if (my < thumbTop) {
                    /* Page up */
                    s_scrollY -= ch;
                    if (s_scrollY < 0) s_scrollY = 0;
                } else {
                    /* Page down */
                    s_scrollY += ch;
                    if (s_scrollY > s_maxScroll) s_scrollY = s_maxScroll;
                }
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }

            Panel& panel = s_panels[s_activeTab];
            int hit = HitTestPanel(panel, mx, my + s_scrollY);
            if (hit < 0) return 0;

            Widget& w = panel.widgets[hit];
            s_focusWidget = hit;

            switch (w.type) {
            case WidgetType::Toggle:
                if (w.boolVal) {
                    *w.boolVal = !*w.boolVal;
                    /* Special: Enabled toggle syncs to shared state */
                    if (w.id == WID_GEN_ENABLED && s_state) {
                        s_state->enabled.store(*w.boolVal, std::memory_order_release);
                        if (!*w.boolVal) s_state->ClearAllKeys();
                    }
                    if (w.id == WID_GEN_SWALLOW) {
                        hook::SetSwallowKeys(*w.boolVal);
                    }
                    AutoSave();
                }
                InvalidateRect(hwnd, NULL, FALSE);
                break;

            case WidgetType::Slider:
                /* Begin drag */
                w.state = WState::Press;
                s_dragging = true;
                s_dragWidget = hit;
                if (w.intVal) {
                    *w.intVal = SliderValueFromX(w, mx);
                }
                SetCapture(hwnd);
                InvalidateRect(hwnd, NULL, FALSE);
                break;

            case WidgetType::KeyBadge:
                if (KeyBadgeHitRemap(w, mx, my + s_scrollY)) {
                    /* Cancel existing capture if any */
                    if (s_captureTarget >= 0) EndCapture(false);
                    StartCapture(hit);
                }
                break;

            case WidgetType::LinkButton:
                w.state = WState::Press;
                InvalidateRect(hwnd, NULL, FALSE);
                break;

            default:
                break;
            }
        }
        return 0;
    }

    /* ---- Left button up ---- */
    case WM_LBUTTONUP: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);

        if (s_scrollDragging) {
            s_scrollDragging = false;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        if (s_dragging) {
            s_dragging = false;
            ReleaseCapture();
            Panel& panel = s_panels[s_activeTab];
            if (s_dragWidget >= 0 && s_dragWidget < panel.count) {
                Widget& w = panel.widgets[s_dragWidget];
                w.state = WState::Normal;
                /* Save slider value */
                SyncSlidersToConfig();
                AutoSave();
            }
            s_dragWidget = -1;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }

        /* Link button activation */
        Panel& panel = s_panels[s_activeTab];
        int hit = HitTestPanel(panel, mx, my + s_scrollY);
        if (hit >= 0 && hit < panel.count) {
            Widget& w = panel.widgets[hit];
            if (w.type == WidgetType::LinkButton && w.state == WState::Press) {
                w.state = WState::Normal;
                HandleLinkAction(w.linkAction);
            }
        }
        return 0;
    }

    /* ---- Double-click (slider reset to default) ---- */
    case WM_LBUTTONDBLCLK: {
        int mx = (short)LOWORD(lParam);
        int my = (short)HIWORD(lParam);

        Panel& panel = s_panels[s_activeTab];
        int hit = HitTestPanel(panel, mx, my + s_scrollY);
        if (hit >= 0 && hit < panel.count) {
            Widget& w = panel.widgets[hit];
            if (w.type == WidgetType::Slider && w.intVal) {
                *w.intVal = w.sliderDefault;
                SyncSlidersToConfig();
                AutoSave();
                InvalidateRect(hwnd, NULL, FALSE);
            }
        }
        return 0;
    }

    /* ---- Mouse wheel ---- */
    case WM_MOUSEWHEEL: {
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        
        /* High precision scrolling using accumulator */
        s_wheelAccumulator += delta;
        while (s_wheelAccumulator >= WHEEL_DELTA) {
            s_scrollY -= g_theme.spacing.RowH;
            s_wheelAccumulator -= WHEEL_DELTA;
        }
        while (s_wheelAccumulator <= -WHEEL_DELTA) {
            s_scrollY += g_theme.spacing.RowH;
            s_wheelAccumulator += WHEEL_DELTA;
        }
        
        if (s_scrollY < 0) s_scrollY = 0;
        if (s_scrollY > s_maxScroll) s_scrollY = s_maxScroll;
        
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    /* ---- Keyboard navigation ---- */
    case WM_KEYDOWN: {
        int vk = (int)wParam;

        /* ESC: cancel capture or close window */
        if (vk == VK_ESCAPE) {
            if (s_captureTarget >= 0) {
                EndCapture(false);
            } else {
                ShowWindow(hwnd, SW_HIDE);
            }
            return 0;
        }

        /* Ctrl+R: reset all */
        if (vk == 'R' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            HandleLinkAction(LINK_RESET_ALL);
            return 0;
        }

        Panel& panel = s_panels[s_activeTab];

        /* Tab / Shift+Tab: cycle focus */
        if (vk == VK_TAB) {
            bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            int dir = shift ? -1 : 1;
            int start = s_focusWidget;
            int next = start;
            for (int attempt = 0; attempt < panel.count; ++attempt) {
                next += dir;
                if (next >= panel.count) next = 0;
                if (next < 0) next = panel.count - 1;
                Widget& w = panel.widgets[next];
                if (!w.visible) continue;
                if (w.type == WidgetType::Toggle || w.type == WidgetType::Slider ||
                    w.type == WidgetType::KeyBadge || w.type == WidgetType::LinkButton) {
                    s_focusWidget = next;
                    
                    /* Auto-scroll to focused widget */
                    if (w.bounds.top < s_scrollY) {
                        s_scrollY = w.bounds.top - g_theme.spacing.LG;
                        if (s_scrollY < 0) s_scrollY = 0;
                    }
                    RECT clientRc;
                    GetClientRect(hwnd, &clientRc);
                    int ch = clientRc.bottom;
                    if (w.bounds.bottom > s_scrollY + ch) {
                        s_scrollY = w.bounds.bottom - ch + g_theme.spacing.LG;
                        if (s_scrollY > s_maxScroll) s_scrollY = s_maxScroll;
                    }

                    InvalidateRect(hwnd, NULL, FALSE);
                    break;
                }
            }
            return 0;
        }

        /* Arrow keys: adjust focused slider */
        if ((vk == VK_LEFT || vk == VK_RIGHT) &&
            s_focusWidget >= 0 && s_focusWidget < panel.count) {
            Widget& w = panel.widgets[s_focusWidget];
            if (w.type == WidgetType::Slider && w.intVal) {
                int step = (vk == VK_RIGHT) ? 1 : -1;
                int newVal = *w.intVal + step;
                if (newVal < w.sliderMin) newVal = w.sliderMin;
                if (newVal > w.sliderMax) newVal = w.sliderMax;
                *w.intVal = newVal;
                SyncSlidersToConfig();
                AutoSave();
                InvalidateRect(hwnd, NULL, FALSE);
                return 0;
            }
        }

        /* Enter/Space: activate focused widget */
        if (vk == VK_RETURN || vk == VK_SPACE) {
            if (s_focusWidget >= 0 && s_focusWidget < panel.count) {
                Widget& w = panel.widgets[s_focusWidget];
                if (w.type == WidgetType::Toggle && w.boolVal) {
                    *w.boolVal = !*w.boolVal;
                    if (w.id == WID_GEN_ENABLED && s_state) {
                        s_state->enabled.store(*w.boolVal, std::memory_order_release);
                        if (!*w.boolVal) s_state->ClearAllKeys();
                    }
                    if (w.id == WID_GEN_SWALLOW) {
                        hook::SetSwallowKeys(*w.boolVal);
                    }
                    AutoSave();
                    InvalidateRect(hwnd, NULL, FALSE);
                } else if (w.type == WidgetType::LinkButton) {
                    HandleLinkAction(w.linkAction);
                } else if (w.type == WidgetType::KeyBadge) {
                    if (s_captureTarget >= 0) EndCapture(false);
                    StartCapture(s_focusWidget);
                }
            }
            return 0;
        }
        break;
    }

    /* ---- Key capture result from hook thread ---- */
    case WM_APP + 10: {
        UINT newVk = (UINT)wParam;
        if (s_captureTarget >= 0 && keys::IsValidBindableVk(newVk)) {
            EndCapture(true, newVk);
            /* Refresh all key badges */
            Panel& p = s_panels[1];
            for (int i = 0; i < p.count; ++i) {
                if (p.widgets[i].type == WidgetType::KeyBadge) {
                    UpdateKeyBadgeText(p.widgets[i]);
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
        } else {
            EndCapture(false);
        }
        return 0;
    }

    /* ---- Timers ---- */
    case WM_TIMER:
        switch (wParam) {
        case TIMER_DIAG:
            if (s_activeTab == 0) SyncEnabledToggle();
            UpdateDiagnostics();
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case TIMER_TOAST: {
            DWORD elapsed = GetTickCount() - s_toastStart;
            if (elapsed < 1500) {
                s_toastFade = 0;  /* visible */
            } else {
                s_toastFade = (int)((elapsed - 1500) / 100) + 1;
                if (s_toastFade >= 5) {
                    s_toastVisible = false;
                    KillTimer(hwnd, TIMER_TOAST);
                }
            }
            InvalidateRect(hwnd, NULL, FALSE);
            break;
        }

        case TIMER_CAPTURE_BLINK:
            s_captureBlink = !s_captureBlink;
            InvalidateRect(hwnd, NULL, FALSE);
            break;

        case TIMER_CAPTURE_TIMEOUT: {
            if (s_captureTarget < 0) {
                KillTimer(hwnd, TIMER_CAPTURE_TIMEOUT);
                break;
            }
            DWORD elapsed = GetTickCount() - s_captureStart;
            int remaining = 3 - (int)(elapsed / 1000);
            if (remaining <= 0) {
                EndCapture(false);
            } else {
                /* Update countdown text */
                Widget& w = s_panels[1].widgets[s_captureTarget];
                _snwprintf(w.valueText, 31, L"%ds", remaining);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            break;
        }
        }
        return 0;

    /* ---- Close (hide, don't destroy) ---- */
    case WM_CLOSE: {
        /* Save window position */
        if (s_config) {
            WINDOWPLACEMENT wp;
            wp.length = sizeof(wp);
            if (GetWindowPlacement(hwnd, &wp)) {
                s_config->winNormL = wp.rcNormalPosition.left;
                s_config->winNormT = wp.rcNormalPosition.top;
                s_config->winNormR = wp.rcNormalPosition.right;
                s_config->winNormB = wp.rcNormalPosition.bottom;
                s_config->winShowCmd = wp.showCmd;
                config::Save(*s_config);
            }
        }
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_DIAG);
        KillTimer(hwnd, TIMER_TOAST);
        KillTimer(hwnd, TIMER_CAPTURE_BLINK);
        KillTimer(hwnd, TIMER_CAPTURE_TIMEOUT);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

/* ================================================================
 * Public API
 * ================================================================ */
void Init(HINSTANCE hInst, SharedState* state, AppConfig* config) {
    s_hInst  = hInst;
    s_state  = state;
    s_config = config;

    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        pfnGetDpiForWindow = (PFN_GetDpiForWindow)GetProcAddress(hUser32, "GetDpiForWindow");
        pfnAdjustWindowRectExForDpi = (PFN_AdjustWindowRectExForDpi)GetProcAddress(hUser32, "AdjustWindowRectExForDpi");
    }

    /* Initialize theme (fonts + brushes) with initial DPI */
    HDC screen = GetDC(NULL);
    int initialDpi = GetDeviceCaps(screen, LOGPIXELSX);
    ReleaseDC(NULL, screen);
    g_theme.Init(initialDpi);

    /* Load DwmSetWindowAttribute dynamically */
    HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
    if (hDwm) {
        pfnDwmSetWindowAttribute = (PFN_DwmSetWindowAttribute)
            GetProcAddress(hDwm, "DwmSetWindowAttribute");
    }

    /* Register the settings window class */
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance      = hInst;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;  /* we paint everything ourselves */
    wc.lpszClassName = s_wndClass;
    RegisterClassExW(&wc);
}

void Show() {
    if (!s_hwnd) {
        /* Determine position */
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
        int w = (int)(WIN_W * g_theme.scale);
        int h = (int)(WIN_H * g_theme.scale);
        
        DWORD dwStyle = WS_OVERLAPPEDWINDOW;
        
        if (s_config && s_config->winNormL != -1) {
            x = s_config->winNormL;
            y = s_config->winNormT;
            w = s_config->winNormR - s_config->winNormL;
            h = s_config->winNormB - s_config->winNormT;

            /* Validate position is on-screen */
            RECT testRc = { x, y, x + w, y + h };
            HMONITOR hMon = MonitorFromRect(&testRc, MONITOR_DEFAULTTONULL);
            if (!hMon) {
                x = CW_USEDEFAULT;
                y = CW_USEDEFAULT;
                w = (int)(WIN_W * g_theme.scale);
                h = (int)(WIN_H * g_theme.scale);
            }
        }
        
        if (x == (int)CW_USEDEFAULT && pfnAdjustWindowRectExForDpi) {
            RECT calcRc = { 0, 0, w, h };
            pfnAdjustWindowRectExForDpi(&calcRc, dwStyle, FALSE, 0, g_theme.currentDpi);
            w = calcRc.right - calcRc.left;
            h = calcRc.bottom - calcRc.top;
        }

        s_hwnd = CreateWindowExW(
            0, s_wndClass, L"CursorMove Settings",
            dwStyle,
            x, y, w, h,
            NULL, NULL, s_hInst, NULL);

        /* Set dark title bar BEFORE first ShowWindow */
        if (s_hwnd && pfnDwmSetWindowAttribute) {
            BOOL dark = TRUE;
            pfnDwmSetWindowAttribute(s_hwnd,
                DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        }
    }

    if (s_hwnd) {
        /* Refresh state */
        SyncEnabledToggle();
        SyncSlidersFromConfig();
        BuildAllPanels();
        LayoutPanel(s_panels[s_activeTab], s_contentRect);

        if (s_config && s_config->winNormL != -1) {
            WINDOWPLACEMENT wp;
            wp.length = sizeof(wp);
            wp.flags = 0;
            wp.showCmd = s_config->winShowCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
            wp.rcNormalPosition = { s_config->winNormL, s_config->winNormT, s_config->winNormR, s_config->winNormB };
            SetWindowPlacement(s_hwnd, &wp);
        } else {
            ShowWindow(s_hwnd, SW_SHOW);
        }
        SetForegroundWindow(s_hwnd);
    }
}

void Hide() {
    if (s_hwnd) ShowWindow(s_hwnd, SW_HIDE);
}

bool IsVisible() {
    return s_hwnd && IsWindowVisible(s_hwnd);
}

void Destroy() {
    if (s_hwnd) {
        DestroyWindow(s_hwnd);
        s_hwnd = NULL;
    }
    g_theme.Destroy();
}

HWND GetHwnd() {
    return s_hwnd;
}

} /* namespace ui */
} /* namespace cm */
