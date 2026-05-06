/*
 * state.h — Atomic shared state between threads.
 *
 * This is the ONLY shared data structure between the hook thread,
 * motion thread, and UI thread. All fields are atomic to avoid
 * any need for mutexes on the hot path.
 *
 * Write rules:
 *   - Hook thread WRITES key states (moveKeys, clickKeys, scroll, precision)
 *   - Motion thread READS key states, WRITES diagnostics
 *   - UI thread READS diagnostics, WRITES enabled/shouldExit
 *   - Config worker WRITES configGeneration
 */
#pragma once
#ifndef CM_STATE_H
#define CM_STATE_H

#include <atomic>
#include <cstdint>
#include "types.h"

namespace cm {

struct SharedState {
    /* ---- Key states (hook → motion) ---- */
    std::atomic<bool> moveKeys[DIR_COUNT];
    std::atomic<bool> clickKeys[BTN_COUNT];
    std::atomic<bool> scrollUp;
    std::atomic<bool> scrollDown;

    /* ---- App-level flags ---- */
    std::atomic<bool>     enabled;
    std::atomic<bool>     precisionMode;
    std::atomic<bool>     shouldExit;

    /* ---- Config versioning ---- */
    std::atomic<uint32_t> configGeneration;

    /* ---- Diagnostics (motion → UI, read every ~500ms) ---- */
    std::atomic<int>      currentTickHz;
    std::atomic<int>      cpuLoadPercent;
    std::atomic<bool>     hookAlive;
    std::atomic<bool>     motionAlive;
    std::atomic<uint32_t> lastError;

    /* ---- Initialize all fields to safe defaults ---- */
    void Init() {
        for (int i = 0; i < DIR_COUNT; ++i)
            moveKeys[i].store(false, std::memory_order_relaxed);
        for (int i = 0; i < BTN_COUNT; ++i)
            clickKeys[i].store(false, std::memory_order_relaxed);
        scrollUp.store(false, std::memory_order_relaxed);
        scrollDown.store(false, std::memory_order_relaxed);
        enabled.store(false, std::memory_order_relaxed);
        precisionMode.store(false, std::memory_order_relaxed);
        shouldExit.store(false, std::memory_order_relaxed);
        configGeneration.store(0, std::memory_order_relaxed);
        currentTickHz.store(0, std::memory_order_relaxed);
        cpuLoadPercent.store(0, std::memory_order_relaxed);
        hookAlive.store(false, std::memory_order_relaxed);
        motionAlive.store(false, std::memory_order_relaxed);
        lastError.store(0, std::memory_order_relaxed);
    }

    /* ---- Clear all held keys (used on sleep/lock/panic) ---- */
    void ClearAllKeys() {
        for (int i = 0; i < DIR_COUNT; ++i)
            moveKeys[i].store(false, std::memory_order_relaxed);
        for (int i = 0; i < BTN_COUNT; ++i)
            clickKeys[i].store(false, std::memory_order_relaxed);
        scrollUp.store(false, std::memory_order_relaxed);
        scrollDown.store(false, std::memory_order_relaxed);
        precisionMode.store(false, std::memory_order_relaxed);
    }
};

} /* namespace cm */

#endif /* CM_STATE_H */
