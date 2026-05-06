/*
 * types.h — Shared types and constants for CursorMove.
 * Included by every translation unit. No Windows.h dependency here.
 */
#pragma once
#ifndef CM_TYPES_H
#define CM_TYPES_H

#include <cstdint>

namespace cm {

/* ---- Direction enum (movement axes) ---- */
enum class Direction : int {
    Up    = 0,
    Down  = 1,
    Left  = 2,
    Right = 3
};
constexpr int DIR_COUNT = 4;

/* ---- Click button enum ---- */
enum class ClickButton : int {
    Left   = 0,
    Right  = 1,
    Middle = 2
};
constexpr int BTN_COUNT = 3;

/* ---- Application mode ---- */
enum class AppMode : int {
    Normal    = 0,
    Precision = 1,
    Scroll    = 2
};

/* ---- Tick-rate bounds ---- */
constexpr int MIN_TICK_HZ     = 60;
constexpr int DEFAULT_TICK_HZ = 90;
constexpr int MAX_TICK_HZ     = 120;

/* ---- Motion tuning parameters ---- */
struct MotionParams {
    float baseSpeed;            /* pixels/sec at the start of a hold       */
    float maxSpeed;             /* pixels/sec after full acceleration      */
    float accelTimeMs;          /* ms to reach maxSpeed                    */
    float decelTimeMs;          /* ms to coast to zero after release       */
    float precisionMultiplier;  /* speed scale in precision mode           */
    float smoothingAccel;       /* lerp factor while accelerating          */
    float smoothingDecel;       /* lerp factor while decelerating          */
    int   tickHz;               /* target tick rate                        */

    MotionParams()
        : baseSpeed(180.0f)
        , maxSpeed(1200.0f)
        , accelTimeMs(250.0f)
        , decelTimeMs(140.0f)
        , precisionMultiplier(0.35f)
        , smoothingAccel(0.15f)
        , smoothingDecel(0.25f)
        , tickHz(DEFAULT_TICK_HZ)
    {}
};

} /* namespace cm */

#endif /* CM_TYPES_H */
