/*
 * motion.h — Adaptive motion engine thread.
 *
 * Reads atomic key states, computes smooth acceleration/deceleration,
 * and sends mouse input via SendInput. Uses real dt, never assumes
 * a fixed interval. Self-throttles between 60–120 Hz.
 */
#pragma once
#ifndef CM_MOTION_H
#define CM_MOTION_H

#include "util.h"
#include "state.h"
#include "types.h"

namespace cm {
namespace motion {

    /* Start the motion engine thread.
     * state: shared atomic state to read key states from.
     * params: initial motion tuning parameters.
     * Returns thread handle, or NULL on failure. */
    HANDLE Start(SharedState* state, const MotionParams& params);

    /* Signal the motion thread to stop and wait.
     * Timeout in milliseconds. */
    void Stop(HANDLE hThread, DWORD timeoutMs = 3000);

    /* Update motion parameters (thread-safe).
     * Called from config worker or UI thread. */
    void UpdateParams(const MotionParams& newParams);

} /* namespace motion */
} /* namespace cm */

#endif /* CM_MOTION_H */
