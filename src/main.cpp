/*
 * main.cpp — CursorMove entry point.
 *
 * Phase 1: Creates a hidden window, tray icon, and message loop.
 * Later phases add hook thread, motion thread, config, and settings GUI.
 */
#include "util.h"
#include "types.h"
#include "state.h"
#include "tray.h"
#include "hook.h"
#include "motion.h"
#include "config.h"
#include "hotkey.h"
#include "ui.h"

#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <commctrl.h>   /* InitCommonControlsEx */

/* WTS session notification constants — may be missing in MinGW 6.3 */
#ifndef WM_WTSSESSION_CHANGE
#define WM_WTSSESSION_CHANGE        0x02B1
#endif
#ifndef WTS_SESSION_LOCK
#define WTS_SESSION_LOCK            0x7
#endif
#ifndef WTS_SESSION_UNLOCK
#define WTS_SESSION_UNLOCK          0x8
#endif
#ifndef NOTIFY_FOR_THIS_SESSION
#define NOTIFY_FOR_THIS_SESSION     0
#endif

/* WTS functions — loaded dynamically to avoid link errors on MinGW 6.3 */
typedef BOOL (WINAPI *PFN_WTSRegister)(HWND, DWORD);
typedef BOOL (WINAPI *PFN_WTSUnRegister)(HWND);
static PFN_WTSRegister   pfnWTSRegister   = NULL;
static PFN_WTSUnRegister pfnWTSUnRegister = NULL;

static void LoadWTSFunctions() {
    HMODULE hWtsApi = LoadLibraryW(L"wtsapi32.dll");
    if (hWtsApi) {
        pfnWTSRegister   = reinterpret_cast<PFN_WTSRegister>(
            GetProcAddress(hWtsApi, "WTSRegisterSessionNotification"));
        pfnWTSUnRegister = reinterpret_cast<PFN_WTSUnRegister>(
            GetProcAddress(hWtsApi, "WTSUnRegisterSessionNotification"));
        /* Don't FreeLibrary — keep it loaded for the process lifetime */
    }
}

/* SetProcessDPIAware — loaded dynamically for XP compat (not critical) */
typedef BOOL (WINAPI *PFN_SetDPIAware)(void);
static void SetDPIAwareSafe() {
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    if (hUser32) {
        PFN_SetDPIAware pfn = reinterpret_cast<PFN_SetDPIAware>(
            GetProcAddress(hUser32, "SetProcessDPIAware"));
        if (pfn) pfn();
    }
}

#include "../res/resource.h"

/* ---- Globals (lifetime = process) ---- */
static cm::SharedState  g_state;
static cm::AppConfig    g_config;
static HWND             g_hwndMain      = NULL;
static HICON           g_hIconApp   = NULL;
static HANDLE          g_hHookThread   = NULL;
static HANDLE          g_hMotionThread = NULL;
static const wchar_t*  g_className  = L"CursorMoveMainWnd";

/* ---- Forward declarations ---- */
static LRESULT CALLBACK MainWndProc(HWND, UINT, WPARAM, LPARAM);
static bool             RegisterMainClass(HINSTANCE hInst);
static HWND             CreateMainWindow(HINSTANCE hInst);
static void             UpdateTrayTooltip();
static void             Shutdown();

/* ================================================================
 * WinMain — Application entry point
 * ================================================================ */
int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE /* hPrevInstance */,
                   LPSTR     /* lpCmdLine */,
                   int       /* nCmdShow */)
{
    /* ---- Prevent multiple instances ---- */
    HANDLE hMutex = CreateMutexW(NULL, TRUE, L"Global\\CursorMoveSingleInstance");
    if (hMutex == NULL || GetLastError() == ERROR_ALREADY_EXISTS) {
        MessageBoxW(NULL, L"CursorMove is already running.",
                    L"CursorMove", MB_OK | MB_ICONINFORMATION);
        if (hMutex) CloseHandle(hMutex);
        return 0;
    }

    /* ---- DPI awareness (safe dynamic load) ---- */
    SetDPIAwareSafe();

    /* ---- Load WTS functions dynamically ---- */
    LoadWTSFunctions();

    /* ---- Initialize common controls for modern look ---- */
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES | ICC_TAB_CLASSES |
                 ICC_BAR_CLASSES | ICC_UPDOWN_CLASS;
    if (!InitCommonControlsEx(&icc)) {
        /* Non-fatal: controls may look old-style */
    }

    /* ---- Initialize shared state ---- */
    g_state.Init();

    /* ---- Load configuration ---- */
    g_config = cm::config::Load();
    g_state.enabled.store(g_config.enabled, std::memory_order_relaxed);

    /* ---- Load application icon (use system default for now) ---- */
    g_hIconApp = LoadIcon(NULL, IDI_APPLICATION);

    /* ---- Register window class & create hidden message window ---- */
    if (!RegisterMainClass(hInstance)) {
        cm::util::ShowLastError(L"CursorMove - Failed to register window class");
        CloseHandle(hMutex);
        return 1;
    }

    g_hwndMain = CreateMainWindow(hInstance);
    if (!g_hwndMain) {
        cm::util::ShowLastError(L"CursorMove - Failed to create message window");
        CloseHandle(hMutex);
        return 1;
    }

    /* ---- Create tray icon ---- */
    if (!cm::tray::Create(g_hwndMain, g_hIconApp)) {
        cm::util::ShowError(L"CursorMove",
            L"Could not create tray icon. The app is running but may be "
            L"hard to access. Right-click the taskbar to find it.");
    }

    /* ---- Start keyboard hook thread ---- */
    g_hHookThread = cm::hook::Start(&g_state);
    if (!g_hHookThread) {
        cm::util::ShowError(L"CursorMove",
            L"Could not start keyboard hook. The app will run but "
            L"keyboard control will not work.");
    } else {
        /* Apply configured key bindings */
        cm::hook::UpdateBindings(g_config.keys);
    }

    /* ---- Initialize settings UI ---- */
    cm::ui::Init(hInstance, &g_state, &g_config);

    /* ---- Start motion engine thread ---- */
    g_hMotionThread = cm::motion::Start(&g_state, g_config.motion);
    if (!g_hMotionThread) {
        cm::util::ShowError(L"CursorMove",
            L"Could not start motion engine.");
    }

    /* ---- Register global hotkeys (toggle + panic) ---- */
    if (!cm::hotkey::Register(g_hwndMain, g_config.toggleHotkey,
                              g_config.panicHotkey)) {
        cm::util::ShowError(L"CursorMove",
            L"Could not register the panic hotkey (Ctrl+Alt+Esc).\n"
            L"Another application may be using it.\n"
            L"The app will still work but may be harder to emergency-stop.");
    }
    if (!cm::hotkey::IsToggleRegistered()) {
        cm::util::ShowError(L"CursorMove",
            L"Could not register toggle hotkey. You can still toggle\n"
            L"from the tray icon context menu.");
    }

    /* ---- Register for session change notifications (lock/unlock) ---- */
    if (pfnWTSRegister) {
        pfnWTSRegister(g_hwndMain, NOTIFY_FOR_THIS_SESSION);
    }

    /* ---- Main message loop ---- */
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    /* ---- Cleanup ---- */
    Shutdown();
    cm::ui::Destroy();
    cm::hotkey::Unregister(g_hwndMain);
    if (pfnWTSUnRegister) {
        pfnWTSUnRegister(g_hwndMain);
    }
    cm::tray::Destroy(g_hwndMain);
    DestroyWindow(g_hwndMain);
    CloseHandle(hMutex);

    return static_cast<int>(msg.wParam);
}

/* ================================================================
 * Window class registration
 * ================================================================ */
static bool RegisterMainClass(HINSTANCE hInst) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance      = hInst;
    wc.hIcon         = g_hIconApp;
    wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = g_className;

    return RegisterClassExW(&wc) != 0;
}

/* ================================================================
 * Create the hidden message window
 * ================================================================ */
static HWND CreateMainWindow(HINSTANCE hInst) {
    return CreateWindowExW(
        0,
        g_className,
        L"CursorMove",
        WS_OVERLAPPEDWINDOW,   /* style doesn't matter — window is hidden */
        CW_USEDEFAULT, CW_USEDEFAULT,
        CW_USEDEFAULT, CW_USEDEFAULT,
        NULL,   /* no parent */
        NULL,   /* no menu */
        hInst,
        NULL    /* no create param */
    );
    /* Window is NOT shown — it only exists to receive messages */
}

/* ================================================================
 * Update tray tooltip based on current state
 * ================================================================ */
static void UpdateTrayTooltip() {
    bool en = g_state.enabled.load(std::memory_order_relaxed);
    cm::tray::SetTooltip(g_hwndMain,
        en ? L"CursorMove - Enabled" : L"CursorMove - Disabled");
}

/* ================================================================
 * Shutdown: signal all threads and wait
 * ================================================================ */
static void Shutdown() {
    g_state.shouldExit.store(true, std::memory_order_release);
    g_state.ClearAllKeys();
    g_state.enabled.store(false, std::memory_order_relaxed);

    /* Stop motion thread first (so it stops sending input) */
    cm::motion::Stop(g_hMotionThread, 3000);
    g_hMotionThread = NULL;

    /* Stop hook thread */
    cm::hook::Stop(g_hHookThread, 3000);
    g_hHookThread = NULL;
}

/* ================================================================
 * Main window procedure
 * ================================================================ */
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam)
{
    switch (msg) {

    /* ---- Global hotkey events ---- */
    case WM_HOTKEY:
        if (wParam == cm::hotkey::HOTKEY_TOGGLE) {
            /* Toggle enable/disable */
            bool now = !g_state.enabled.load(std::memory_order_relaxed);
            g_state.enabled.store(now, std::memory_order_release);
            if (!now) {
                g_state.ClearAllKeys();
            }
            UpdateTrayTooltip();
            return 0;
        }
        else if (wParam == cm::hotkey::HOTKEY_PANIC) {
            /* PANIC: force-disable and clear everything */
            g_state.ClearAllKeys();
            g_state.enabled.store(false, std::memory_order_release);
            UpdateTrayTooltip();
            return 0;
        }
        break;

    /* ---- Tray icon callback ---- */
    case WM_TRAYICON:
        switch (LOWORD(lParam)) {
        case WM_RBUTTONUP:
            cm::tray::ShowContextMenu(hwnd,
                g_state.enabled.load(std::memory_order_relaxed));
            return 0;

        case WM_LBUTTONDBLCLK:
            cm::ui::Show();
            return 0;
        }
        break;

    /* ---- Menu commands ---- */
    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case ID_TRAY_ENABLE: {
            bool now = !g_state.enabled.load(std::memory_order_relaxed);
            g_state.enabled.store(now, std::memory_order_release);
            if (!now) {
                g_state.ClearAllKeys();
            }
            UpdateTrayTooltip();
            return 0;
        }

        case ID_TRAY_SETTINGS:
            cm::ui::Show();
            return 0;

        case ID_TRAY_RESET: {
            g_config = cm::config::Default();
            cm::config::Validate(g_config);
            cm::hook::UpdateBindings(g_config.keys);
            cm::motion::UpdateParams(g_config.motion);
            cm::config::Save(g_config);
            return 0;
        }

        case ID_TRAY_EXIT:
            PostQuitMessage(0);
            return 0;
        }
        break;

    /* ---- Session change: lock / unlock ---- */
    case WM_WTSSESSION_CHANGE:
        switch (wParam) {
        case WTS_SESSION_LOCK:
            g_state.ClearAllKeys();
            g_state.enabled.store(false, std::memory_order_release);
            UpdateTrayTooltip();
            break;

        case WTS_SESSION_UNLOCK:
            /* Stay disabled after unlock — user must re-enable */
            break;
        }
        return 0;

    /* ---- Power events: sleep / resume ---- */
    case WM_POWERBROADCAST:
        switch (wParam) {
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            g_state.ClearAllKeys();
            g_state.enabled.store(false, std::memory_order_release);
            UpdateTrayTooltip();
            break;
        }
        return TRUE;

    /* ---- Prevent accidental close ---- */
    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
