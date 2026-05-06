/*
 * hook.h — Low-level keyboard hook thread.
 *
 * Runs WH_KEYBOARD_LL on a dedicated thread with its own message loop.
 * Only writes atomic key states — never does physics, UI, or I/O.
 */
#pragma once
#ifndef CM_HOOK_H
#define CM_HOOK_H

#include "util.h"
#include "state.h"
#include "types.h"

namespace cm {
namespace hook {

    /* Key binding map: which VK code maps to which action.
     * Written by config worker (under mutex), read by hook callback. */
    struct KeyBindings {
        UINT moveUp;
        UINT moveDown;
        UINT moveLeft;
        UINT moveRight;
        UINT clickLeft;
        UINT clickRight;
        UINT clickMiddle;
        UINT scrollUp;
        UINT scrollDown;
        UINT precisionToggle;  /* hold key for precision mode */

        KeyBindings()
            : moveUp('W'), moveDown('S'), moveLeft('A'), moveRight('D')
            , clickLeft(VK_SPACE), clickRight('E'), clickMiddle('Q')
            , scrollUp('R'), scrollDown('F')
            , precisionToggle(VK_LSHIFT)
        {}
    };

    /* Start the hook thread. Returns the thread handle.
     * state: shared atomic state to write key states into.
     * Returns NULL on failure. */
    HANDLE Start(SharedState* state);

    /* Signal the hook thread to stop and wait for it.
     * Timeout in milliseconds. */
    void Stop(HANDLE hThread, DWORD timeoutMs = 3000);

    /* Update the active key bindings (thread-safe).
     * Called from config worker or UI thread. */
    void UpdateBindings(const KeyBindings& newBindings);

} /* namespace hook */
} /* namespace cm */

#endif /* CM_HOOK_H */
