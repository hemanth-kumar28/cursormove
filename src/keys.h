/*
 * keys.h — Virtual-key code ↔ name mapping.
 *
 * Maps human-readable key names (like "W", "Space", "LShift")
 * to Windows VK codes and back. Used by config and GUI.
 */
#pragma once
#ifndef CM_KEYS_H
#define CM_KEYS_H

#include "util.h"   /* windows.h */
#include <string>

namespace cm {
namespace keys {

    /* Convert a key name string to a VK code.
     * Returns 0 if the name is unrecognized. Case-insensitive. */
    UINT VkFromName(const std::string& name);

    /* Convert a VK code to a display name.
     * Returns "Unknown" for unrecognized codes. */
    std::string NameFromVk(UINT vk);

    /* Returns true if the VK code is valid and safe to bind. */
    bool IsValidBindableVk(UINT vk);

} /* namespace keys */
} /* namespace cm */

#endif /* CM_KEYS_H */
