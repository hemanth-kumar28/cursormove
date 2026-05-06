/*
 * motion.cpp — Adaptive motion engine implementation.
 *
 * 2-stage motion model:
 *   Stage A (hold): cubic ease-out acceleration from baseSpeed to maxSpeed
 *   Stage B (release): exponential decay to zero
 *
 * Key design decisions:
 *   - Real dt from QueryPerformanceCounter, never assumed fixed
 *   - Sub-pixel accumulator for smooth low-speed movement
 *   - Adaptive tick rate: 60–120 Hz, self-adjusting
 *   - Click/scroll fire on TRANSITIONS only, never every frame
 *   - SleepEx-based timing, never busy-spin
 *   - SendInput failure tracking with auto-disable
 */
#include "motion.h"
#include <process.h>   /* _beginthreadex */
#include <cmath>

namespace cm {
namespace motion {

/* ---- Module-level state ---- */
static SharedState* s_state    = NULL;
static volatile bool s_stop    = false;

/* Parameters: protected by a critical section for atomic swap */
static CRITICAL_SECTION s_paramsCS;
static MotionParams     s_params;
static MotionParams     s_paramsLocal;
static volatile bool    s_paramsDirty = true;

/* ---- High-resolution timer ---- */
static LARGE_INTEGER s_qpcFreq;

static double QpcSeconds() {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) /
           static_cast<double>(s_qpcFreq.QuadPart);
}

/* ---- Clamp helper ---- */
static float Clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Refresh params snapshot if changed ---- */
static void RefreshParams() {
    if (s_paramsDirty) {
        EnterCriticalSection(&s_paramsCS);
        s_paramsLocal = s_params;
        s_paramsDirty = false;
        LeaveCriticalSection(&s_paramsCS);
    }
}

/* ---- Send relative mouse movement via SendInput ---- */
static bool SendMouseMove(int dx, int dy) {
    if (dx == 0 && dy == 0) return true;

    INPUT inp;
    ZeroMemory(&inp, sizeof(inp));
    inp.type           = INPUT_MOUSE;
    inp.mi.dx          = dx;
    inp.mi.dy          = dy;
    inp.mi.dwFlags     = MOUSEEVENTF_MOVE;

    UINT sent = SendInput(1, &inp, sizeof(INPUT));
    return sent == 1;
}

/* ---- Send mouse button event ---- */
static bool SendMouseButton(ClickButton btn, bool down) {
    INPUT inp;
    ZeroMemory(&inp, sizeof(inp));
    inp.type = INPUT_MOUSE;

    switch (btn) {
    case ClickButton::Left:
        inp.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        break;
    case ClickButton::Right:
        inp.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
        break;
    case ClickButton::Middle:
        inp.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
        break;
    default:
        return false;
    }

    UINT sent = SendInput(1, &inp, sizeof(INPUT));
    return sent == 1;
}

/* ---- Send mouse wheel event ---- */
static bool SendMouseWheel(int delta) {
    if (delta == 0) return true;

    INPUT inp;
    ZeroMemory(&inp, sizeof(inp));
    inp.type           = INPUT_MOUSE;
    inp.mi.dwFlags     = MOUSEEVENTF_WHEEL;
    inp.mi.mouseData   = static_cast<DWORD>(delta);

    UINT sent = SendInput(1, &inp, sizeof(INPUT));
    return sent == 1;
}

/* ================================================================
 * Motion engine thread entry point
 * ================================================================ */
static unsigned int __stdcall MotionThreadProc(void* param) {
    (void)param;

    QueryPerformanceFrequency(&s_qpcFreq);

    /* ---- Per-direction state ---- */
    float holdTime[DIR_COUNT]    = {0};  /* seconds held */
    float currentSpeed[DIR_COUNT]= {0};  /* current speed (px/s) */
    float accumX = 0.0f, accumY = 0.0f; /* sub-pixel accumulators */

    /* ---- Click transition tracking ---- */
    bool clickWasDown[BTN_COUNT] = {false, false, false};

    /* ---- Scroll state ---- */
    float scrollAccum = 0.0f;
    const float SCROLL_SPEED = 600.0f;  /* wheel units per second */

    /* ---- Adaptive tick state ---- */
    int  currentTickHz = DEFAULT_TICK_HZ;
    int  consecutiveSlow = 0;
    int  consecutiveFast = 0;

    /* ---- SendInput failure tracking ---- */
    int  consecutiveFailures = 0;
    const int MAX_SEND_FAILURES = 50;

    /* ---- Timing ---- */
    double lastTime = QpcSeconds();

    if (s_state) {
        s_state->motionAlive.store(true, std::memory_order_release);
    }

    /* ================================================================
     * Main loop
     * ================================================================ */
    while (!s_stop && !s_state->shouldExit.load(std::memory_order_relaxed)) {

        double nowTime = QpcSeconds();
        float dt = static_cast<float>(nowTime - lastTime);
        lastTime = nowTime;

        /* Safety clamp dt — handles sleep/resume spikes */
        dt = Clampf(dt, 0.0001f, 0.1f);

        /* Refresh parameters if config changed */
        RefreshParams();
        const MotionParams& p = s_paramsLocal;

        /* Update adaptive tick target */
        currentTickHz = util::ClampInt(currentTickHz, MIN_TICK_HZ, MAX_TICK_HZ);

        bool isEnabled = s_state->enabled.load(std::memory_order_relaxed);

        if (!isEnabled) {
            /* When disabled: decay all speeds, clear accumulators */
            for (int i = 0; i < DIR_COUNT; ++i) {
                holdTime[i]    = 0.0f;
                currentSpeed[i] = 0.0f;
            }
            accumX = accumY = 0.0f;
            scrollAccum = 0.0f;

            /* Release any held mouse buttons */
            for (int i = 0; i < BTN_COUNT; ++i) {
                if (clickWasDown[i]) {
                    SendMouseButton(static_cast<ClickButton>(i), false);
                    clickWasDown[i] = false;
                }
            }

            /* Sleep longer when disabled to save CPU */
            Sleep(50);
            continue;
        }

        bool precision = s_state->precisionMode.load(std::memory_order_relaxed);
        float speedMult = precision ? p.precisionMultiplier : 1.0f;

        /* ---- Stage A + B: Acceleration / Deceleration per direction ---- */
        float vx = 0.0f, vy = 0.0f;

        for (int i = 0; i < DIR_COUNT; ++i) {
            bool keyDown = s_state->moveKeys[i].load(std::memory_order_relaxed);
            float targetSpeed = 0.0f;

            if (keyDown) {
                /* Stage A: Hold-time acceleration */
                holdTime[i] += dt;

                float accelTimeSec = p.accelTimeMs / 1000.0f;
                if (accelTimeSec < 0.01f) accelTimeSec = 0.01f;

                float t = Clampf(holdTime[i] / accelTimeSec, 0.0f, 1.0f);

                /* Cubic ease-out: 1 - (1 - t)^3 */
                float oneMinusT = 1.0f - t;
                float curve = 1.0f - (oneMinusT * oneMinusT * oneMinusT);

                targetSpeed = p.baseSpeed + (p.maxSpeed - p.baseSpeed) * curve;
                targetSpeed *= speedMult;
            } else {
                /* Key released: reset hold timer */
                holdTime[i] = 0.0f;
                targetSpeed = 0.0f;
            }

            /* Stage B: Smooth transition (separate accel/decel rates) */
            float smoothFactor;
            if (targetSpeed > currentSpeed[i]) {
                smoothFactor = Clampf(p.smoothingAccel, 0.01f, 1.0f);
            } else {
                smoothFactor = Clampf(p.smoothingDecel, 0.01f, 1.0f);
            }

            /* Frame-rate-independent smoothing:
             * Use 1 - (1 - factor)^(dt * targetHz) approximation */
            float effectiveSmooth = 1.0f - static_cast<float>(
                pow(1.0 - smoothFactor, dt * currentTickHz));
            effectiveSmooth = Clampf(effectiveSmooth, 0.0f, 1.0f);

            currentSpeed[i] += (targetSpeed - currentSpeed[i]) * effectiveSmooth;

            /* Kill tiny residuals to avoid floating-point drift */
            if (currentSpeed[i] < 0.5f && targetSpeed == 0.0f) {
                currentSpeed[i] = 0.0f;
            }
        }

        /* Convert per-direction speeds to velocity vector */
        vx = currentSpeed[static_cast<int>(Direction::Right)]
           - currentSpeed[static_cast<int>(Direction::Left)];
        vy = currentSpeed[static_cast<int>(Direction::Down)]
           - currentSpeed[static_cast<int>(Direction::Up)];

        /* ---- Sub-pixel accumulation and SendInput ---- */
        accumX += vx * dt;
        accumY += vy * dt;

        int dx = static_cast<int>(accumX);
        int dy = static_cast<int>(accumY);
        accumX -= static_cast<float>(dx);
        accumY -= static_cast<float>(dy);

        if (dx != 0 || dy != 0) {
            bool ok = SendMouseMove(dx, dy);
            if (ok) {
                consecutiveFailures = 0;
            } else {
                consecutiveFailures++;
                if (consecutiveFailures >= MAX_SEND_FAILURES) {
                    /* Disable motion — something is seriously wrong */
                    s_state->enabled.store(false, std::memory_order_release);
                    s_state->lastError.store(GetLastError(),
                                             std::memory_order_relaxed);
                    consecutiveFailures = 0;
                }
            }
        }

        /* ---- Click handling: transitions only ---- */
        for (int i = 0; i < BTN_COUNT; ++i) {
            bool isDown = s_state->clickKeys[i].load(std::memory_order_relaxed);

            if (isDown && !clickWasDown[i]) {
                /* Key-down transition → mouse button down */
                SendMouseButton(static_cast<ClickButton>(i), true);
            } else if (!isDown && clickWasDown[i]) {
                /* Key-up transition → mouse button up */
                SendMouseButton(static_cast<ClickButton>(i), false);
            }

            clickWasDown[i] = isDown;
        }

        /* ---- Scroll handling: continuous while held ---- */
        {
            bool scrUp   = s_state->scrollUp.load(std::memory_order_relaxed);
            bool scrDown = s_state->scrollDown.load(std::memory_order_relaxed);

            float scrollTarget = 0.0f;
            if (scrUp)   scrollTarget += SCROLL_SPEED;
            if (scrDown) scrollTarget -= SCROLL_SPEED;

            scrollTarget *= speedMult;

            scrollAccum += scrollTarget * dt;

            /* Send in WHEEL_DELTA (120) increments */
            int scrollDelta = static_cast<int>(scrollAccum / 40.0f) * 40;
            if (scrollDelta != 0) {
                SendMouseWheel(scrollDelta);
                scrollAccum -= static_cast<float>(scrollDelta);
            }
        }

        /* ---- Adaptive tick rate adjustment ---- */
        double frameEnd = QpcSeconds();
        double frameMs = (frameEnd - nowTime) * 1000.0;
        double targetMs = 1000.0 / currentTickHz;

        if (frameMs > targetMs * 1.5) {
            consecutiveSlow++;
            consecutiveFast = 0;
            if (consecutiveSlow > 10 && currentTickHz > MIN_TICK_HZ) {
                currentTickHz -= 5;
                if (currentTickHz < MIN_TICK_HZ)
                    currentTickHz = MIN_TICK_HZ;
                consecutiveSlow = 0;
            }
        } else if (frameMs < targetMs * 0.3) {
            consecutiveFast++;
            consecutiveSlow = 0;
            if (consecutiveFast > 30 && currentTickHz < p.tickHz) {
                currentTickHz += 1;
                if (currentTickHz > MAX_TICK_HZ)
                    currentTickHz = MAX_TICK_HZ;
                consecutiveFast = 0;
            }
        } else {
            consecutiveSlow = 0;
            consecutiveFast = 0;
        }

        /* Update diagnostics */
        s_state->currentTickHz.store(currentTickHz, std::memory_order_relaxed);

        /* ---- Sleep until next tick ---- */
        double sleepMs = targetMs - (frameEnd - nowTime) * 1000.0;
        if (sleepMs > 1.0) {
            Sleep(static_cast<DWORD>(sleepMs));
        } else {
            Sleep(1);  /* Yield at minimum — never busy-spin */
        }
    }

    /* ---- Cleanup: release any held mouse buttons ---- */
    for (int i = 0; i < BTN_COUNT; ++i) {
        if (clickWasDown[i]) {
            SendMouseButton(static_cast<ClickButton>(i), false);
        }
    }

    if (s_state) {
        s_state->motionAlive.store(false, std::memory_order_release);
    }

    return 0;
}

/* ================================================================
 * Public API
 * ================================================================ */

HANDLE Start(SharedState* state, const MotionParams& params) {
    if (!state) return NULL;

    s_state = state;
    s_stop  = false;

    InitializeCriticalSection(&s_paramsCS);
    s_params      = params;
    s_paramsLocal = params;
    s_paramsDirty = false;

    unsigned int tid = 0;
    HANDLE hThread = reinterpret_cast<HANDLE>(
        _beginthreadex(NULL, 0, MotionThreadProc, NULL, 0, &tid));

    return hThread;
}

void Stop(HANDLE hThread, DWORD timeoutMs) {
    if (!hThread) return;

    s_stop = true;

    DWORD result = WaitForSingleObject(hThread, timeoutMs);
    if (result == WAIT_TIMEOUT) {
        TerminateThread(hThread, 1);
    }

    CloseHandle(hThread);
    DeleteCriticalSection(&s_paramsCS);
}

void UpdateParams(const MotionParams& newParams) {
    EnterCriticalSection(&s_paramsCS);
    s_params = newParams;
    s_paramsDirty = true;
    LeaveCriticalSection(&s_paramsCS);
}

} /* namespace motion */
} /* namespace cm */
