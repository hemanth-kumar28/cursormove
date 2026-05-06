/*
 * keys.cpp — VK code ↔ name lookup tables.
 */
#include "keys.h"
#include <cctype>
#include <cstring>

namespace cm {
namespace keys {

/* ---- Key name → VK code table ---- */
struct KeyEntry {
    const char* name;
    UINT        vk;
};

static const KeyEntry g_keyTable[] = {
    /* Letters */
    {"A", 'A'}, {"B", 'B'}, {"C", 'C'}, {"D", 'D'},
    {"E", 'E'}, {"F", 'F'}, {"G", 'G'}, {"H", 'H'},
    {"I", 'I'}, {"J", 'J'}, {"K", 'K'}, {"L", 'L'},
    {"M", 'M'}, {"N", 'N'}, {"O", 'O'}, {"P", 'P'},
    {"Q", 'Q'}, {"R", 'R'}, {"S", 'S'}, {"T", 'T'},
    {"U", 'U'}, {"V", 'V'}, {"W", 'W'}, {"X", 'X'},
    {"Y", 'Y'}, {"Z", 'Z'},

    /* Numbers */
    {"0", '0'}, {"1", '1'}, {"2", '2'}, {"3", '3'},
    {"4", '4'}, {"5", '5'}, {"6", '6'}, {"7", '7'},
    {"8", '8'}, {"9", '9'},

    /* Function keys */
    {"F1", VK_F1}, {"F2", VK_F2}, {"F3", VK_F3}, {"F4", VK_F4},
    {"F5", VK_F5}, {"F6", VK_F6}, {"F7", VK_F7}, {"F8", VK_F8},
    {"F9", VK_F9}, {"F10", VK_F10}, {"F11", VK_F11}, {"F12", VK_F12},

    /* Navigation */
    {"Up",     VK_UP},     {"Down",  VK_DOWN},
    {"Left",   VK_LEFT},   {"Right", VK_RIGHT},
    {"Home",   VK_HOME},   {"End",   VK_END},
    {"PageUp", VK_PRIOR},  {"PageDown", VK_NEXT},
    {"Insert", VK_INSERT}, {"Delete",   VK_DELETE},

    /* Modifiers */
    {"LShift",   VK_LSHIFT},   {"RShift",   VK_RSHIFT},
    {"Shift",    VK_SHIFT},
    {"LControl", VK_LCONTROL}, {"RControl", VK_RCONTROL},
    {"Control",  VK_CONTROL},  {"Ctrl",     VK_CONTROL},
    {"LAlt",     VK_LMENU},    {"RAlt",     VK_RMENU},
    {"Alt",      VK_MENU},
    {"LWin",     VK_LWIN},     {"RWin",     VK_RWIN},

    /* Common keys */
    {"Space",     VK_SPACE},
    {"Enter",     VK_RETURN},
    {"Return",    VK_RETURN},
    {"Tab",       VK_TAB},
    {"Escape",    VK_ESCAPE},
    {"Esc",       VK_ESCAPE},
    {"Backspace", VK_BACK},
    {"CapsLock",  VK_CAPITAL},
    {"NumLock",   VK_NUMLOCK},
    {"ScrollLock",VK_SCROLL},
    {"PrintScreen", VK_SNAPSHOT},
    {"Pause",     VK_PAUSE},

    /* Punctuation / symbols */
    {"Semicolon",    VK_OEM_1},      /* ;: */
    {"Equal",        VK_OEM_PLUS},   /* =+ */
    {"Comma",        VK_OEM_COMMA},  /* ,< */
    {"Minus",        VK_OEM_MINUS},  /* -_ */
    {"Period",       VK_OEM_PERIOD}, /* .> */
    {"Slash",        VK_OEM_2},      /* /? */
    {"Backquote",    VK_OEM_3},      /* `~ */
    {"LeftBracket",  VK_OEM_4},      /* [{ */
    {"Backslash",    VK_OEM_5},      /* \| */
    {"RightBracket", VK_OEM_6},      /* ]} */
    {"Quote",        VK_OEM_7},      /* '" */

    /* Numpad */
    {"Numpad0", VK_NUMPAD0}, {"Numpad1", VK_NUMPAD1},
    {"Numpad2", VK_NUMPAD2}, {"Numpad3", VK_NUMPAD3},
    {"Numpad4", VK_NUMPAD4}, {"Numpad5", VK_NUMPAD5},
    {"Numpad6", VK_NUMPAD6}, {"Numpad7", VK_NUMPAD7},
    {"Numpad8", VK_NUMPAD8}, {"Numpad9", VK_NUMPAD9},
    {"NumpadMultiply", VK_MULTIPLY},
    {"NumpadAdd",      VK_ADD},
    {"NumpadSubtract", VK_SUBTRACT},
    {"NumpadDecimal",  VK_DECIMAL},
    {"NumpadDivide",   VK_DIVIDE},
};

static const int g_keyTableSize =
    static_cast<int>(sizeof(g_keyTable) / sizeof(g_keyTable[0]));

/* ---- Case-insensitive string compare ---- */
static bool StrEqualNoCase(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower(static_cast<unsigned char>(*a)) !=
            tolower(static_cast<unsigned char>(*b)))
            return false;
        ++a; ++b;
    }
    return *a == *b;
}

UINT VkFromName(const std::string& name) {
    if (name.empty()) return 0;

    for (int i = 0; i < g_keyTableSize; ++i) {
        if (StrEqualNoCase(name.c_str(), g_keyTable[i].name)) {
            return g_keyTable[i].vk;
        }
    }
    return 0;
}

std::string NameFromVk(UINT vk) {
    if (vk == 0) return "None";

    for (int i = 0; i < g_keyTableSize; ++i) {
        if (g_keyTable[i].vk == vk) {
            return g_keyTable[i].name;
        }
    }
    return "Unknown";
}

bool IsValidBindableVk(UINT vk) {
    /* Reject obviously dangerous or system-reserved keys */
    if (vk == 0) return false;
    if (vk == VK_LWIN || vk == VK_RWIN) return false;  /* system keys */
    if (vk == VK_CAPITAL) return false;  /* caps lock is special */

    /* Must exist in our table to be valid */
    for (int i = 0; i < g_keyTableSize; ++i) {
        if (g_keyTable[i].vk == vk) return true;
    }
    return false;
}

} /* namespace keys */
} /* namespace cm */
