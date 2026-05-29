/*
 * ui_widgets.h — Retained-mode widget system for CursorMove settings UI.
 *
 * Everything is GDI-based. No child HWNDs for controls — the entire
 * content area is owner-drawn on a single backbuffer.
 *
 * Widget types:  SectionHeader, Toggle, Slider, KeyBadge, KeyCapture,
 *                Label, MetaLabel, StatusRow, LinkButton, Separator, Spacer.
 */
#pragma once
#ifndef CM_UI_WIDGETS_H
#define CM_UI_WIDGETS_H

#include "util.h"
#include <cstdint>

namespace cm {

/* ================================================================
 * Color palette — Catppuccin Mocha
 * ================================================================ */
namespace clr {
    static const COLORREF Crust    = RGB(17,  17,  27);
    static const COLORREF Base     = RGB(30,  30,  46);
    static const COLORREF Surface0 = RGB(49,  50,  68);
    static const COLORREF Surface1 = RGB(69,  71,  90);
    static const COLORREF Surface2 = RGB(88,  91, 112);

    static const COLORREF Text     = RGB(205, 214, 244);
    static const COLORREF Subtext1 = RGB(186, 194, 222);
    static const COLORREF Subtext0 = RGB(166, 173, 200);
    static const COLORREF Overlay0 = RGB(108, 112, 134);

    static const COLORREF Accent   = RGB(180, 190, 254);  /* Lavender */
    static const COLORREF Green    = RGB(166, 227, 161);
    static const COLORREF Red      = RGB(243, 139, 168);
    static const COLORREF Yellow   = RGB(249, 226, 175);
}

/* ================================================================
 * Theme — fonts, brushes, and scaled spacing
 * ================================================================ */
struct ScaledSpacing {
    int XS;
    int SM;
    int MD;
    int LG;
    int XL;
    int SidebarW;
    int RowH;
};

struct Theme {
    float scale;
    int currentDpi;
    ScaledSpacing spacing;

    HFONT fontSection;   /* 13px Segoe UI Semibold */
    HFONT fontBody;      /* 12px Segoe UI Regular  */
    HFONT fontMeta;      /* 11px Segoe UI Regular  */
    HFONT fontMono;      /* 12px Consolas Regular   */
    HFONT fontBadge;     /* 11px Segoe UI Semibold */

    HBRUSH brCrust;
    HBRUSH brBase;
    HBRUSH brSurface0;
    HBRUSH brSurface1;
    HBRUSH brSurface2;
    HBRUSH brAccent;
    HBRUSH brGreen;
    HBRUSH brRed;
    HBRUSH brYellow;
    HBRUSH brText;

    bool initialized;

    Theme() : scale(1.0f), currentDpi(96), fontSection(NULL), fontBody(NULL), fontMeta(NULL),
              fontMono(NULL), fontBadge(NULL),
              brCrust(NULL), brBase(NULL), brSurface0(NULL),
              brSurface1(NULL), brSurface2(NULL), brAccent(NULL),
              brGreen(NULL), brRed(NULL), brYellow(NULL), brText(NULL),
              initialized(false) {}

    void Init(int dpi);
    void Destroy();
};

/* Global theme instance */
extern Theme g_theme;

/* ================================================================
 * Widget types
 * ================================================================ */
enum class WidgetType {
    SectionHeader,  /* 13px semibold text + 1px separator */
    Toggle,         /* pill-shaped on/off switch with label */
    Slider,         /* track + filled progress + thumb + value */
    KeyBadge,       /* key-cap display + Remap link */
    KeyCapture,     /* transient: "Press a key..." with pulse */
    Label,          /* 12px body text */
    MetaLabel,      /* 11px subdued text */
    StatusRow,      /* label + colored dot + monospace value */
    LinkButton,     /* clickable text link */
    Separator,      /* 1px line + vertical space */
    Spacer          /* empty vertical space */
};

/* ================================================================
 * Widget state
 * ================================================================ */
enum class WState {
    Normal   = 0,
    Hover    = 1,
    Press    = 2,
    Focus    = 3,
    Disabled = 4
};

/* ================================================================
 * Widget — a single UI element in the retained-mode tree
 * ================================================================ */
static const int WIDGET_TEXT_MAX = 64;

struct Widget {
    WidgetType  type;
    WState      state;
    RECT        bounds;          /* computed by layout, in content-local coords */
    int         id;              /* unique identifier for this widget */
    wchar_t     label[WIDGET_TEXT_MAX];
    wchar_t     valueText[32];   /* formatted value (slider), or badge text */
    bool        visible;

    /* ---- Toggle ---- */
    bool*       boolVal;         /* pointer to the config bool */

    /* ---- Slider ---- */
    int*        intVal;          /* pointer to the config int */
    int         sliderMin;
    int         sliderMax;
    int         sliderDefault;
    const wchar_t* sliderUnit;  /* "px/s", "ms", "%", "Hz", etc. */

    /* ---- StatusRow ---- */
    int         statusColor;     /* 0 = no dot, 1 = green, 2 = red */

    /* ---- Spacer ---- */
    int         spacerH;         /* height in pixels */

    /* ---- KeyBadge/KeyCapture ---- */
    UINT*       vkVal;           /* pointer to the VK code in config */

    /* ---- LinkButton ---- */
    int         linkAction;      /* action identifier for click */

    Widget();
};

/* ================================================================
 * Panel — a collection of widgets for one tab
 * ================================================================ */
static const int MAX_WIDGETS = 48;

struct Panel {
    Widget widgets[MAX_WIDGETS];
    int    count;

    Panel() : count(0) {}

    /* Add a widget, returns pointer to it (or NULL if full) */
    Widget* Add(WidgetType type, int id, const wchar_t* label);
};

/* ================================================================
 * Layout — compute widget bounds from top to bottom
 * ================================================================ */
void LayoutPanel(Panel& panel, const RECT& contentRect);

/* ================================================================
 * Rendering — paint one widget to a DC
 * ================================================================ */
void PaintWidget(HDC hdc, const Widget& w, const Theme& t,
                 bool isFocused, DWORD captureBlinkOn);

/* Paint sidebar to DC */
void PaintSidebar(HDC hdc, const RECT& sidebarRect,
                  int activeTab, int hoverTab, const Theme& t);

/* Paint the "Saved" toast */
void PaintToast(HDC hdc, const RECT& contentRect, const Theme& t,
                int fadeStep);  /* 0 = full, 1-4 = fading, 5+ = hidden */

/* ================================================================
 * Hit testing
 * ================================================================ */

/* Returns widget index or -1 */
int HitTestPanel(const Panel& panel, int x, int y);

/* Returns sidebar tab index (0–4) or -1 */
int HitTestSidebar(const RECT& sidebarRect, int x, int y);

/* For sliders: returns the value for a given X position */
int SliderValueFromX(const Widget& w, int x);

/* For sliders: returns the thumb center X for current value */
int SliderThumbX(const Widget& w);

/* For sliders: returns true if (x,y) is on the thumb */
bool SliderHitThumb(const Widget& w, int x, int y);

/* For key badges: returns true if (x,y) is on the "Remap" link */
bool KeyBadgeHitRemap(const Widget& w, int x, int y);

/* ================================================================
 * Drawing helpers (internal, exposed for flexibility)
 * ================================================================ */
void FillRoundRect(HDC hdc, const RECT& rc, int radius, HBRUSH br);
void DrawRoundRectOutline(HDC hdc, const RECT& rc, int radius, COLORREF color);

} /* namespace cm */

#endif /* CM_UI_WIDGETS_H */
