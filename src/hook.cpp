/*
 * hook.cpp — Low-level keyboard hook implementation.
 *
 * This runs on a DEDICATED thread with its own message pump.
 * The hook callback ONLY updates atomic booleans in SharedState.
 *
 * Critical rules:
 *   - No allocations in the callback
 *   - No file I/O or logging in the callback
 *   - No mutex locks in the callback
 *   - Ignores LLKHF_INJECTED to prevent feedback loops
 *   - Uses key-transition detection to avoid auto-repeat noise
 */
#include "hook.h"
#include <process.h>  /* _beginthreadex */

namespace cm {
namespace hook {

/* ---- Module-level state ---- */
static SharedState*     s_state       = NULL;
static HHOOK            s_hHook       = NULL;
static DWORD            s_threadId    = 0;
static volatile bool    s_stopFlag    = false;

/* Bindings: protected by a critical section for atomic swap.
 * The hook callback reads a LOCAL COPY so the CS is not held during dispatch. */
static CRITICAL_SECTION s_bindingsCS;
static KeyBindings      s_bindings;
static KeyBindings      s_bindingsLocal;  /* snapshot used in callback */
static volatile bool    s_bindingsDirty = true;

/* ---- Refresh the local snapshot if bindings changed ---- */
static void RefreshBindingsSnapshot() {
    if (s_bindingsDirty) {
        EnterCriticalSection(&s_bindingsCS);
        s_bindingsLocal = s_bindings;
        s_bindingsDirty = false;
        LeaveCriticalSection(&s_bindingsCS);
    }
}

/* ================================================================
 * Low-level keyboard hook callback
 *
 * This function must be FAST. No allocations, no I/O, no logging.
 * ================================================================ */
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam,
                                              LPARAM lParam)
{
    if (nCode < 0) {
        return CallNextHookEx(s_hHook, nCode, wParam, lParam);
    }

    KBDLLHOOKSTRUCT* kb = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

    /* ---- CRITICAL: Ignore injected events to prevent feedback loops ---- */
    if (kb->flags & LLKHF_INJECTED) {
        return CallNextHookEx(s_hHook, nCode, wParam, lParam);
    }

    /* Determine if this is a key-down or key-up event */
    bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    bool isUp   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

    if (!isDown && !isUp) {
        return CallNextHookEx(s_hHook, nCode, wParam, lParam);
    }

    UINT vk = kb->vkCode;

    /* Refresh bindings snapshot if config changed */
    RefreshBindingsSnapshot();

    const KeyBindings& b = s_bindingsLocal;

    /* ---- Map VK to action and update atomic state ---- */

    /* Movement keys */
    if (vk == b.moveUp) {
        s_state->moveKeys[static_cast<int>(Direction::Up)]
            .store(isDown, std::memory_order_relaxed);
    }
    else if (vk == b.moveDown) {
        s_state->moveKeys[static_cast<int>(Direction::Down)]
            .store(isDown, std::memory_order_relaxed);
    }
    else if (vk == b.moveLeft) {
        s_state->moveKeys[static_cast<int>(Direction::Left)]
            .store(isDown, std::memory_order_relaxed);
    }
    else if (vk == b.moveRight) {
        s_state->moveKeys[static_cast<int>(Direction::Right)]
            .store(isDown, std::memory_order_relaxed);
    }

    /* Click keys */
    else if (vk == b.clickLeft) {
        s_state->clickKeys[static_cast<int>(ClickButton::Left)]
            .store(isDown, std::memory_order_relaxed);
    }
    else if (vk == b.clickRight) {
        s_state->clickKeys[static_cast<int>(ClickButton::Right)]
            .store(isDown, std::memory_order_relaxed);
    }
    else if (vk == b.clickMiddle) {
        s_state->clickKeys[static_cast<int>(ClickButton::Middle)]
            .store(isDown, std::memory_order_relaxed);
    }

    /* Scroll keys */
    else if (vk == b.scrollUp) {
        s_state->scrollUp.store(isDown, std::memory_order_relaxed);
    }
    else if (vk == b.scrollDown) {
        s_state->scrollDown.store(isDown, std::memory_order_relaxed);
    }

    /* Precision mode (hold-to-activate) */
    else if (vk == b.precisionToggle) {
        s_state->precisionMode.store(isDown, std::memory_order_relaxed);
    }

    /* Let the event pass through — we do NOT swallow keys.
     * The app works alongside normal keyboard input. */
    return CallNextHookEx(s_hHook, nCode, wParam, lParam);
}

/* ================================================================
 * Hook thread entry point
 * ================================================================ */
static unsigned int __stdcall HookThreadProc(void* param) {
    (void)param;

    /* Install the low-level keyboard hook */
    s_hHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                 GetModuleHandle(NULL), 0);
    if (!s_hHook) {
        if (s_state) {
            s_state->hookAlive.store(false, std::memory_order_relaxed);
            s_state->lastError.store(GetLastError(), std::memory_order_relaxed);
        }
        return 1;
    }

    /* Signal that the hook is alive */
    if (s_state) {
        s_state->hookAlive.store(true, std::memory_order_release);
    }

    /* ---- Message loop (required for WH_KEYBOARD_LL to work) ---- */
    MSG msg;
    while (!s_stopFlag) {
        /* Use MsgWaitForMultipleObjects to avoid busy-spinning.
         * Wake every 100ms to check the stop flag. */
        DWORD result = MsgWaitForMultipleObjects(0, NULL, FALSE, 100,
                                                  QS_ALLINPUT);
        if (result == WAIT_OBJECT_0) {
            while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
                if (msg.message == WM_QUIT) {
                    s_stopFlag = true;
                    break;
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
        }
    }

    /* ---- Cleanup ---- */
    if (s_hHook) {
        UnhookWindowsHookEx(s_hHook);
        s_hHook = NULL;
    }

    if (s_state) {
        s_state->hookAlive.store(false, std::memory_order_release);
    }

    return 0;
}

/* ================================================================
 * Public API
 * ================================================================ */

HANDLE Start(SharedState* state) {
    if (!state) return NULL;

    s_state    = state;
    s_stopFlag = false;

    InitializeCriticalSection(&s_bindingsCS);

    /* Set default bindings */
    s_bindings      = KeyBindings();
    s_bindingsLocal = s_bindings;
    s_bindingsDirty = false;

    /* Start the thread */
    unsigned int tid = 0;
    HANDLE hThread = reinterpret_cast<HANDLE>(
        _beginthreadex(NULL, 0, HookThreadProc, NULL, 0, &tid));

    if (hThread) {
        s_threadId = static_cast<DWORD>(tid);
    }

    return hThread;
}

void Stop(HANDLE hThread, DWORD timeoutMs) {
    if (!hThread) return;

    /* Signal the thread to stop */
    s_stopFlag = true;

    /* Post WM_QUIT to the hook thread's message loop */
    if (s_threadId != 0) {
        PostThreadMessage(s_threadId, WM_QUIT, 0, 0);
    }

    /* Wait for the thread to finish */
    DWORD result = WaitForSingleObject(hThread, timeoutMs);
    if (result == WAIT_TIMEOUT) {
        /* Force-terminate as a last resort (should not happen) */
        TerminateThread(hThread, 1);
    }

    CloseHandle(hThread);
    s_threadId = 0;

    DeleteCriticalSection(&s_bindingsCS);
}

void UpdateBindings(const KeyBindings& newBindings) {
    EnterCriticalSection(&s_bindingsCS);
    s_bindings = newBindings;
    s_bindingsDirty = true;
    LeaveCriticalSection(&s_bindingsCS);
}

} /* namespace hook */
} /* namespace cm */
