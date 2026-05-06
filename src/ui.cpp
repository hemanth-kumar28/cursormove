/*
 * ui.cpp — Settings window with 4 tabs: General, Keys, Motion, Diagnostics.
 * Pure Win32 — no frameworks.
 */
#include "ui.h"
#include "hook.h"
#include "motion.h"
#include "hotkey.h"
#include "keys.h"

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <commctrl.h>
#include "../res/resource.h"

namespace cm {
namespace ui {

/* ---- Module state ---- */
static HINSTANCE   s_hInst   = NULL;
static SharedState* s_state  = NULL;
static AppConfig*  s_config  = NULL;
static HWND        s_hwnd    = NULL;
static HWND        s_tab     = NULL;
static HWND        s_panels[4] = {};
static int         s_curTab  = 0;
static UINT_PTR    s_diagTimer = 0;
static const wchar_t* s_wndClass = L"CursorMoveSettingsWnd";

/* Control IDs */
enum {
    IDC_TAB = 2001,
    /* General */
    IDC_CHK_ENABLED = 2010, IDC_LBL_TOGGLE, IDC_LBL_PANIC, IDC_CHK_STARTUP,
    /* Motion sliders */
    IDC_SLD_BASE = 2030, IDC_SLD_MAX, IDC_SLD_ACCEL, IDC_SLD_DECEL,
    IDC_SLD_PREC, IDC_SLD_SMOOTH, IDC_SLD_TICK,
    IDC_VAL_BASE, IDC_VAL_MAX, IDC_VAL_ACCEL, IDC_VAL_DECEL,
    IDC_VAL_PREC, IDC_VAL_SMOOTH, IDC_VAL_TICK,
    /* Keys remap buttons */
    IDC_BTN_UP = 2060, IDC_BTN_DOWN, IDC_BTN_LEFT, IDC_BTN_RIGHT,
    IDC_BTN_CLKL, IDC_BTN_CLKR, IDC_BTN_CLKM,
    IDC_BTN_SCRU, IDC_BTN_SCRD,
    IDC_LBL_UP, IDC_LBL_DOWN, IDC_LBL_LEFT, IDC_LBL_RIGHT,
    IDC_LBL_CLKL, IDC_LBL_CLKR, IDC_LBL_CLKM,
    IDC_LBL_SCRU, IDC_LBL_SCRD,
    /* Diag labels */
    IDC_DIAG_HOOK = 2100, IDC_DIAG_MOTION, IDC_DIAG_ERR,
    IDC_DIAG_TICK, IDC_DIAG_CPU, IDC_DIAG_MODE,
    /* Save button */
    IDC_BTN_SAVE = 2200
};

static LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);

/* ---- Helper: create a static label ---- */
static HWND MakeLabel(HWND parent, const wchar_t* text,
                      int x, int y, int w, int h, int id = 0) {
    return CreateWindowW(L"STATIC", text, WS_CHILD|WS_VISIBLE|SS_LEFT,
                         x, y, w, h, parent, (HMENU)(intptr_t)id, s_hInst, NULL);
}

/* ---- Helper: create a button ---- */
static HWND MakeButton(HWND parent, const wchar_t* text,
                       int x, int y, int w, int h, int id) {
    return CreateWindowW(L"BUTTON", text, WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
                         x, y, w, h, parent, (HMENU)(intptr_t)id, s_hInst, NULL);
}

/* ---- Helper: create a checkbox ---- */
static HWND MakeCheck(HWND parent, const wchar_t* text,
                      int x, int y, int w, int h, int id) {
    return CreateWindowW(L"BUTTON", text,
                         WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX,
                         x, y, w, h, parent, (HMENU)(intptr_t)id, s_hInst, NULL);
}

/* ---- Helper: create a trackbar (slider) ---- */
static HWND MakeSlider(HWND parent, int x, int y, int w, int h,
                       int id, int minVal, int maxVal, int curVal) {
    HWND sl = CreateWindowW(TRACKBAR_CLASSW, L"",
        WS_CHILD|WS_VISIBLE|TBS_HORZ|TBS_AUTOTICKS,
        x, y, w, h, parent, (HMENU)(intptr_t)id, s_hInst, NULL);
    if (sl) {
        SendMessage(sl, TBM_SETRANGE, TRUE, MAKELONG(minVal, maxVal));
        SendMessage(sl, TBM_SETPOS, TRUE, curVal);
    }
    return sl;
}

/* ---- Helper: set label to narrow string ---- */
static void SetLabelA(HWND hwnd, int id, const char* text) {
    wchar_t buf[64];
    for (int i = 0; i < 63 && text[i]; ++i) {
        buf[i] = (wchar_t)text[i];
        buf[i+1] = 0;
    }
    SetDlgItemTextW(hwnd, id, buf);
}

/* ---- Show the selected tab panel ---- */
static void ShowTab(int idx) {
    for (int i = 0; i < 4; ++i) {
        if (s_panels[i])
            ShowWindow(s_panels[i], (i == idx) ? SW_SHOW : SW_HIDE);
    }
    s_curTab = idx;
}

/* ---- Update key label text from config ---- */
static void RefreshKeyLabels() {
    if (!s_config || !s_panels[1]) return;
    HWND p = s_panels[1];
    SetLabelA(p, IDC_LBL_UP,    keys::NameFromVk(s_config->keys.moveUp).c_str());
    SetLabelA(p, IDC_LBL_DOWN,  keys::NameFromVk(s_config->keys.moveDown).c_str());
    SetLabelA(p, IDC_LBL_LEFT,  keys::NameFromVk(s_config->keys.moveLeft).c_str());
    SetLabelA(p, IDC_LBL_RIGHT, keys::NameFromVk(s_config->keys.moveRight).c_str());
    SetLabelA(p, IDC_LBL_CLKL,  keys::NameFromVk(s_config->keys.clickLeft).c_str());
    SetLabelA(p, IDC_LBL_CLKR,  keys::NameFromVk(s_config->keys.clickRight).c_str());
    SetLabelA(p, IDC_LBL_CLKM,  keys::NameFromVk(s_config->keys.clickMiddle).c_str());
    SetLabelA(p, IDC_LBL_SCRU,  keys::NameFromVk(s_config->keys.scrollUp).c_str());
    SetLabelA(p, IDC_LBL_SCRD,  keys::NameFromVk(s_config->keys.scrollDown).c_str());
}

/* ---- Update slider value labels ---- */
static void RefreshSliderLabels() {
    if (!s_panels[2]) return;
    HWND p = s_panels[2];
    wchar_t buf[32];
    auto setVal = [&](int sliderId, int lblId, const wchar_t* fmt) {
        HWND sl = GetDlgItem(p, sliderId);
        if (!sl) return;
        int v = (int)SendMessage(sl, TBM_GETPOS, 0, 0);
        _snwprintf(buf, 32, fmt, v);
        SetDlgItemTextW(p, lblId, buf);
    };
    setVal(IDC_SLD_BASE,  IDC_VAL_BASE,  L"%d px/s");
    setVal(IDC_SLD_MAX,   IDC_VAL_MAX,   L"%d px/s");
    setVal(IDC_SLD_ACCEL, IDC_VAL_ACCEL, L"%d ms");
    setVal(IDC_SLD_DECEL, IDC_VAL_DECEL, L"%d ms");
    setVal(IDC_SLD_TICK,  IDC_VAL_TICK,  L"%d Hz");
    /* Precision: stored as pct (5-100) */
    {
        HWND sl = GetDlgItem(p, IDC_SLD_PREC);
        if (sl) {
            int v = (int)SendMessage(sl, TBM_GETPOS, 0, 0);
            _snwprintf(buf, 32, L"%d%%", v);
            SetDlgItemTextW(p, IDC_VAL_PREC, buf);
        }
    }
    /* Smoothing: stored as pct (1-100) */
    {
        HWND sl = GetDlgItem(p, IDC_SLD_SMOOTH);
        if (sl) {
            int v = (int)SendMessage(sl, TBM_GETPOS, 0, 0);
            _snwprintf(buf, 32, L"%d%%", v);
            SetDlgItemTextW(p, IDC_VAL_SMOOTH, buf);
        }
    }
}

/* ---- Create the General tab panel ---- */
static HWND CreateGeneralPanel(HWND parent) {
    HWND p = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_CLIPSIBLINGS,
                           5, 30, 460, 330, parent, NULL, s_hInst, NULL);
    HWND chk = MakeCheck(p, L"Enabled", 20, 15, 120, 22, IDC_CHK_ENABLED);
    if (s_state && s_state->enabled.load(std::memory_order_relaxed))
        SendMessage(chk, BM_SETCHECK, BST_CHECKED, 0);

    MakeLabel(p, L"Toggle hotkey:", 20, 50, 120, 20);
    MakeLabel(p, L"Alt+S", 150, 50, 200, 20, IDC_LBL_TOGGLE);

    MakeLabel(p, L"Panic hotkey:", 20, 80, 120, 20);
    MakeLabel(p, L"Ctrl+Alt+Esc", 150, 80, 200, 20, IDC_LBL_PANIC);

    MakeCheck(p, L"Start with Windows", 20, 115, 200, 22, IDC_CHK_STARTUP);
    return p;
}

/* ---- Create the Keys tab panel ---- */
static HWND CreateKeysPanel(HWND parent) {
    HWND p = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_CLIPSIBLINGS,
                           5, 30, 460, 330, parent, NULL, s_hInst, NULL);
    struct Row { const wchar_t* label; int btnId; int lblId; };
    Row rows[] = {
        {L"Move Up",     IDC_BTN_UP,   IDC_LBL_UP},
        {L"Move Down",   IDC_BTN_DOWN, IDC_LBL_DOWN},
        {L"Move Left",   IDC_BTN_LEFT, IDC_LBL_LEFT},
        {L"Move Right",  IDC_BTN_RIGHT,IDC_LBL_RIGHT},
        {L"Click Left",  IDC_BTN_CLKL, IDC_LBL_CLKL},
        {L"Click Right", IDC_BTN_CLKR, IDC_LBL_CLKR},
        {L"Click Middle",IDC_BTN_CLKM, IDC_LBL_CLKM},
        {L"Scroll Up",   IDC_BTN_SCRU, IDC_LBL_SCRU},
        {L"Scroll Down", IDC_BTN_SCRD, IDC_LBL_SCRD},
    };
    int y = 10;
    for (int i = 0; i < 9; ++i) {
        MakeLabel(p, rows[i].label, 20, y+3, 100, 20);
        MakeLabel(p, L"...", 130, y+3, 80, 20, rows[i].lblId);
        MakeButton(p, L"Remap", 220, y, 65, 24, rows[i].btnId);
        y += 30;
    }
    return p;
}

/* ---- Create the Motion tab panel ---- */
static HWND CreateMotionPanel(HWND parent) {
    HWND p = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_CLIPSIBLINGS,
                           5, 30, 460, 330, parent, NULL, s_hInst, NULL);
    const MotionParams& m = s_config->motion;
    struct SlRow {
        const wchar_t* label; int sldId; int valId;
        int lo; int hi; int cur;
    };
    SlRow rows[] = {
        {L"Base speed",   IDC_SLD_BASE,  IDC_VAL_BASE,  50, 500, (int)m.baseSpeed},
        {L"Max speed",    IDC_SLD_MAX,   IDC_VAL_MAX,   200, 3000, (int)m.maxSpeed},
        {L"Accel time",   IDC_SLD_ACCEL, IDC_VAL_ACCEL, 50, 1000, (int)m.accelTimeMs},
        {L"Decel time",   IDC_SLD_DECEL, IDC_VAL_DECEL, 50, 500, (int)m.decelTimeMs},
        {L"Precision %",  IDC_SLD_PREC,  IDC_VAL_PREC,  5, 100, (int)(m.precisionMultiplier*100)},
        {L"Smoothing %",  IDC_SLD_SMOOTH,IDC_VAL_SMOOTH, 1, 100, (int)(m.smoothingAccel*100)},
        {L"Tick rate",    IDC_SLD_TICK,  IDC_VAL_TICK,  60, 120, m.tickHz},
    };
    int y = 10;
    for (int i = 0; i < 7; ++i) {
        MakeLabel(p, rows[i].label, 10, y+5, 90, 18);
        MakeSlider(p, 105, y, 240, 28, rows[i].sldId,
                   rows[i].lo, rows[i].hi, rows[i].cur);
        MakeLabel(p, L"", 355, y+5, 80, 18, rows[i].valId);
        y += 38;
    }
    MakeButton(p, L"Apply && Save", 105, y+5, 130, 28, IDC_BTN_SAVE);
    return p;
}

/* ---- Create the Diagnostics tab panel ---- */
static HWND CreateDiagPanel(HWND parent) {
    HWND p = CreateWindowW(L"STATIC", L"", WS_CHILD|WS_CLIPSIBLINGS,
                           5, 30, 460, 330, parent, NULL, s_hInst, NULL);
    struct DRow { const wchar_t* label; int id; };
    DRow rows[] = {
        {L"Hook status:",   IDC_DIAG_HOOK},
        {L"Motion engine:", IDC_DIAG_MOTION},
        {L"Last error:",    IDC_DIAG_ERR},
        {L"Tick rate:",     IDC_DIAG_TICK},
        {L"CPU load:",      IDC_DIAG_CPU},
        {L"Mode:",          IDC_DIAG_MODE},
    };
    int y = 15;
    for (int i = 0; i < 6; ++i) {
        MakeLabel(p, rows[i].label, 20, y, 120, 20);
        MakeLabel(p, L"...", 150, y, 250, 20, rows[i].id);
        y += 30;
    }
    return p;
}

/* ---- Update diagnostics labels ---- */
static void UpdateDiagnostics() {
    if (!s_state || !s_panels[3]) return;
    HWND p = s_panels[3];
    wchar_t buf[64];
    bool hook  = s_state->hookAlive.load(std::memory_order_relaxed);
    bool mot   = s_state->motionAlive.load(std::memory_order_relaxed);
    int  tick  = s_state->currentTickHz.load(std::memory_order_relaxed);
    int  cpu   = s_state->cpuLoadPercent.load(std::memory_order_relaxed);
    DWORD err  = s_state->lastError.load(std::memory_order_relaxed);
    bool en    = s_state->enabled.load(std::memory_order_relaxed);
    bool prec  = s_state->precisionMode.load(std::memory_order_relaxed);

    SetDlgItemTextW(p, IDC_DIAG_HOOK,   hook ? L"Active" : L"Inactive");
    SetDlgItemTextW(p, IDC_DIAG_MOTION, mot ? L"Running" : L"Stopped");
    _snwprintf(buf, 64, L"%lu", (unsigned long)err);
    SetDlgItemTextW(p, IDC_DIAG_ERR, err == 0 ? L"None" : buf);
    _snwprintf(buf, 64, L"%d Hz", tick);
    SetDlgItemTextW(p, IDC_DIAG_TICK, buf);
    _snwprintf(buf, 64, L"%d%%", cpu);
    SetDlgItemTextW(p, IDC_DIAG_CPU, buf);
    SetDlgItemTextW(p, IDC_DIAG_MODE,
        !en ? L"Disabled" : prec ? L"Precision" : L"Normal");
}

/* ---- Remap dialog: capture next key press ---- */
static UINT DoRemapDialog(HWND parent) {
    MessageBoxW(parent,
        L"Press the key you want to assign.\n\n"
        L"(After closing this dialog, press the desired key\n"
        L"within 3 seconds.)",
        L"Remap Key", MB_OK | MB_ICONINFORMATION);

    /* Poll for a new keypress for 3 seconds */
    DWORD start = GetTickCount();
    while (GetTickCount() - start < 3000) {
        for (int vk = 1; vk < 256; ++vk) {
            /* Skip modifiers and mouse buttons */
            if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) continue;
            if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU) continue;
            if (vk == VK_LCONTROL || vk == VK_RCONTROL) continue;
            if (vk == VK_LSHIFT || vk == VK_RSHIFT) continue;
            if (vk == VK_LMENU || vk == VK_RMENU) continue;
            if (vk == VK_ESCAPE) continue;  /* reserved */

            if (GetAsyncKeyState(vk) & 0x8000) {
                /* Wait for release */
                while (GetAsyncKeyState(vk) & 0x8000) Sleep(10);
                if (keys::IsValidBindableVk((UINT)vk))
                    return (UINT)vk;
            }
        }
        Sleep(20);
    }
    return 0;  /* timeout — no change */
}

/* ---- Apply sliders to config ---- */
static void ApplySliders() {
    if (!s_config || !s_panels[2]) return;
    HWND p = s_panels[2];
    auto getPos = [&](int id) -> int {
        HWND sl = GetDlgItem(p, id);
        return sl ? (int)SendMessage(sl, TBM_GETPOS, 0, 0) : 0;
    };
    s_config->motion.baseSpeed       = (float)getPos(IDC_SLD_BASE);
    s_config->motion.maxSpeed        = (float)getPos(IDC_SLD_MAX);
    s_config->motion.accelTimeMs     = (float)getPos(IDC_SLD_ACCEL);
    s_config->motion.decelTimeMs     = (float)getPos(IDC_SLD_DECEL);
    s_config->motion.precisionMultiplier = getPos(IDC_SLD_PREC) / 100.0f;
    s_config->motion.smoothingAccel  = getPos(IDC_SLD_SMOOTH) / 100.0f;
    s_config->motion.smoothingDecel  = s_config->motion.smoothingAccel;
    s_config->motion.tickHz          = getPos(IDC_SLD_TICK);
    config::Validate(*s_config);
    motion::UpdateParams(s_config->motion);
    config::Save(*s_config);
}

/* ---- Handle remap button clicks ---- */
static void HandleRemap(HWND hwnd, int btnId) {
    UINT newVk = DoRemapDialog(hwnd);
    if (newVk == 0) return;

    hook::KeyBindings& k = s_config->keys;
    switch (btnId) {
    case IDC_BTN_UP:    k.moveUp    = newVk; break;
    case IDC_BTN_DOWN:  k.moveDown  = newVk; break;
    case IDC_BTN_LEFT:  k.moveLeft  = newVk; break;
    case IDC_BTN_RIGHT: k.moveRight = newVk; break;
    case IDC_BTN_CLKL:  k.clickLeft = newVk; break;
    case IDC_BTN_CLKR:  k.clickRight= newVk; break;
    case IDC_BTN_CLKM:  k.clickMiddle=newVk; break;
    case IDC_BTN_SCRU:  k.scrollUp  = newVk; break;
    case IDC_BTN_SCRD:  k.scrollDown= newVk; break;
    }
    hook::UpdateBindings(k);
    config::Save(*s_config);
    RefreshKeyLabels();
}

/* ================================================================ */
static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg,
                                        WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        /* Tab control */
        s_tab = CreateWindowW(WC_TABCONTROLW, L"",
            WS_CHILD|WS_VISIBLE|WS_CLIPSIBLINGS,
            0, 0, 470, 370, hwnd, (HMENU)(intptr_t)IDC_TAB, s_hInst, NULL);
        if (s_tab) {
            TCITEMW ti;
            ZeroMemory(&ti, sizeof(ti));
            ti.mask = TCIF_TEXT;
            ti.pszText = (LPWSTR)L"General"; TabCtrl_InsertItem(s_tab, 0, &ti);
            ti.pszText = (LPWSTR)L"Keys";    TabCtrl_InsertItem(s_tab, 1, &ti);
            ti.pszText = (LPWSTR)L"Motion";  TabCtrl_InsertItem(s_tab, 2, &ti);
            ti.pszText = (LPWSTR)L"Diagnostics"; TabCtrl_InsertItem(s_tab, 3, &ti);
        }
        s_panels[0] = CreateGeneralPanel(hwnd);
        s_panels[1] = CreateKeysPanel(hwnd);
        s_panels[2] = CreateMotionPanel(hwnd);
        s_panels[3] = CreateDiagPanel(hwnd);
        RefreshKeyLabels();
        RefreshSliderLabels();
        ShowTab(0);
        /* Diagnostics refresh timer */
        s_diagTimer = SetTimer(hwnd, 1, 500, NULL);
        return 0;

    case WM_TIMER:
        if (wParam == 1) {
            UpdateDiagnostics();
            if (s_panels[2]) RefreshSliderLabels();
        }
        return 0;

    case WM_NOTIFY: {
        NMHDR* hdr = (NMHDR*)lParam;
        if (hdr->idFrom == IDC_TAB && hdr->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(s_tab);
            ShowTab(sel);
        }
        return 0;
    }

    case WM_HSCROLL:
        /* Slider changed */
        RefreshSliderLabels();
        return 0;

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_CHK_ENABLED && s_state) {
            bool chk = (SendDlgItemMessage(hwnd, IDC_CHK_ENABLED,
                         BM_GETCHECK, 0, 0) == BST_CHECKED);
            /* The checkbox is on the General panel, not directly on hwnd */
            HWND ctl = GetDlgItem(s_panels[0], IDC_CHK_ENABLED);
            if (ctl) chk = (SendMessage(ctl, BM_GETCHECK, 0, 0) == BST_CHECKED);
            s_state->enabled.store(chk, std::memory_order_release);
            if (!chk) s_state->ClearAllKeys();
        }
        else if (id == IDC_BTN_SAVE) {
            ApplySliders();
        }
        else if (id >= IDC_BTN_UP && id <= IDC_BTN_SCRD) {
            HandleRemap(hwnd, id);
        }
        return 0;
    }

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        if (s_diagTimer) { KillTimer(hwnd, 1); s_diagTimer = 0; }
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

    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SettingsWndProc;
    wc.hInstance      = hInst;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = s_wndClass;
    RegisterClassExW(&wc);
}

void Show() {
    if (!s_hwnd) {
        s_hwnd = CreateWindowExW(0, s_wndClass, L"CursorMove Settings",
            WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU,
            CW_USEDEFAULT, CW_USEDEFAULT, 485, 410,
            NULL, NULL, s_hInst, NULL);
    }
    if (s_hwnd) {
        ShowWindow(s_hwnd, SW_SHOW);
        SetForegroundWindow(s_hwnd);
        RefreshKeyLabels();
        RefreshSliderLabels();
        UpdateDiagnostics();
    }
}

void Hide() {
    if (s_hwnd) ShowWindow(s_hwnd, SW_HIDE);
}

bool IsVisible() {
    return s_hwnd && IsWindowVisible(s_hwnd);
}

void Destroy() {
    if (s_hwnd) { DestroyWindow(s_hwnd); s_hwnd = NULL; }
}

HWND GetHwnd() { return s_hwnd; }

} /* namespace ui */
} /* namespace cm */
