/*
 * ui_widgets.cpp — GDI rendering for the retained-mode widget system.
 *
 * All drawing is done to a single backbuffer HDC passed in.
 * Double-buffering happens one level up in the settings WNDPROC.
 *
 * MinGW 6.3 / C++14 / pure Win32 GDI.
 */
#include "ui_widgets.h"
#include "keys.h"
#include <cstdio>

/* DwmSetWindowAttribute — loaded dynamically */
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace cm {

/* ================================================================
 * Theme global instance
 * ================================================================ */
Theme g_theme;

static HFONT MakeFont(int height, int weight, const wchar_t* face) {
    return CreateFontW(
        height, 0, 0, 0, weight,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
}

void Theme::Init(int dpi) {
    if (initialized && currentDpi == dpi) return;
    if (initialized) Destroy();

    currentDpi = dpi;
    scale = dpi / 96.0f;

    spacing.XS = (int)(4 * scale);
    spacing.SM = (int)(8 * scale);
    spacing.MD = (int)(16 * scale);
    spacing.LG = (int)(24 * scale);
    spacing.XL = (int)(32 * scale);
    spacing.SidebarW = (int)(148 * scale);
    spacing.RowH = (int)(44 * scale);

    fontSection = MakeFont((int)(-13 * scale), FW_SEMIBOLD, L"Segoe UI");
    fontBody    = MakeFont((int)(-12 * scale), FW_NORMAL,   L"Segoe UI");
    fontMeta    = MakeFont((int)(-11 * scale), FW_NORMAL,   L"Segoe UI");
    fontMono    = MakeFont((int)(-12 * scale), FW_NORMAL,   L"Consolas");
    fontBadge   = MakeFont((int)(-11 * scale), FW_SEMIBOLD, L"Segoe UI");

    brCrust    = CreateSolidBrush(clr::Crust);
    brBase     = CreateSolidBrush(clr::Base);
    brSurface0 = CreateSolidBrush(clr::Surface0);
    brSurface1 = CreateSolidBrush(clr::Surface1);
    brSurface2 = CreateSolidBrush(clr::Surface2);
    brAccent   = CreateSolidBrush(clr::Accent);
    brGreen    = CreateSolidBrush(clr::Green);
    brRed      = CreateSolidBrush(clr::Red);
    brYellow   = CreateSolidBrush(clr::Yellow);
    brText     = CreateSolidBrush(clr::Text);

    initialized = true;
}

void Theme::Destroy() {
    if (!initialized) return;

    if (fontSection) { DeleteObject(fontSection); fontSection = NULL; }
    if (fontBody)    { DeleteObject(fontBody);    fontBody = NULL; }
    if (fontMeta)    { DeleteObject(fontMeta);    fontMeta = NULL; }
    if (fontMono)    { DeleteObject(fontMono);    fontMono = NULL; }
    if (fontBadge)   { DeleteObject(fontBadge);   fontBadge = NULL; }

    if (brCrust)    { DeleteObject(brCrust);    brCrust = NULL; }
    if (brBase)     { DeleteObject(brBase);     brBase = NULL; }
    if (brSurface0) { DeleteObject(brSurface0); brSurface0 = NULL; }
    if (brSurface1) { DeleteObject(brSurface1); brSurface1 = NULL; }
    if (brSurface2) { DeleteObject(brSurface2); brSurface2 = NULL; }
    if (brAccent)   { DeleteObject(brAccent);   brAccent = NULL; }
    if (brGreen)    { DeleteObject(brGreen);    brGreen = NULL; }
    if (brRed)      { DeleteObject(brRed);      brRed = NULL; }
    if (brYellow)   { DeleteObject(brYellow);   brYellow = NULL; }
    if (brText)     { DeleteObject(brText);     brText = NULL; }

    initialized = false;
}

/* ================================================================
 * Widget constructor
 * ================================================================ */
Widget::Widget()
    : type(WidgetType::Spacer)
    , state(WState::Normal)
    , id(0)
    , visible(true)
    , boolVal(NULL)
    , intVal(NULL)
    , sliderMin(0), sliderMax(100), sliderDefault(50)
    , sliderUnit(L"")
    , statusColor(0)
    , spacerH(8) // wait, it's initialized before g_theme is ready, but it's fine
    , vkVal(NULL)
    , linkAction(0)
{
    bounds.left = bounds.top = bounds.right = bounds.bottom = 0;
    label[0] = L'\0';
    valueText[0] = L'\0';
}

/* ================================================================
 * Panel::Add
 * ================================================================ */
Widget* Panel::Add(WidgetType t, int wid, const wchar_t* lbl) {
    if (count >= MAX_WIDGETS) return NULL;
    Widget& w = widgets[count];
    w = Widget();
    w.type = t;
    w.id = wid;
    if (lbl) {
        wcsncpy(w.label, lbl, WIDGET_TEXT_MAX - 1);
        w.label[WIDGET_TEXT_MAX - 1] = L'\0';
    }
    ++count;
    return &w;
}

/* ================================================================
 * Drawing helpers
 * ================================================================ */
void FillRoundRect(HDC hdc, const RECT& rc, int radius, HBRUSH br) {
    HRGN rgn = CreateRoundRectRgn(rc.left, rc.top, rc.right + 1,
                                   rc.bottom + 1, radius, radius);
    if (rgn) {
        FillRgn(hdc, rgn, br);
        DeleteObject(rgn);
    }
}

void DrawRoundRectOutline(HDC hdc, const RECT& rc, int radius, COLORREF color) {
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, oldBr);
    DeleteObject(pen);
}

static void DrawTextLeft(HDC hdc, HFONT font, COLORREF color,
                          int x, int y, int w, int h,
                          const wchar_t* text) {
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    RECT rc = {x, y, x + w, y + h};
    DrawTextW(hdc, text, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}

static void DrawTextRight(HDC hdc, HFONT font, COLORREF color,
                           int x, int y, int w, int h,
                           const wchar_t* text) {
    HFONT old = (HFONT)SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    RECT rc = {x, y, x + w, y + h};
    DrawTextW(hdc, text, -1, &rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}

static void FillSolidRect(HDC hdc, const RECT& rc, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
}

static void DrawDot(HDC hdc, int cx, int cy, int radius, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    HBRUSH old = (HBRUSH)SelectObject(hdc, br);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    Ellipse(hdc, cx - radius, cy - radius, cx + radius, cy + radius);
    SelectObject(hdc, oldPen);
    SelectObject(hdc, old);
    DeleteObject(pen);
    DeleteObject(br);
}

/* ================================================================
 * Layout — compute widget bounds
 * ================================================================ */
void LayoutPanel(Panel& panel, const RECT& contentRect) {
    int x = contentRect.left + g_theme.spacing.XL;
    int y = contentRect.top + g_theme.spacing.LG;
    int contentW = (contentRect.right - contentRect.left) - g_theme.spacing.XL * 2;

    for (int i = 0; i < panel.count; ++i) {
        Widget& w = panel.widgets[i];
        if (!w.visible) {
            w.bounds.left = w.bounds.right = 0;
            w.bounds.top = w.bounds.bottom = 0;
            continue;
        }

        w.bounds.left = x;
        w.bounds.top = y;
        w.bounds.right = x + contentW;

        int h = 0;
        switch (w.type) {
        case WidgetType::SectionHeader:
            if (i > 0) y += g_theme.spacing.LG;  /* extra top margin between sections */
            w.bounds.top = y;
            h = (int)(20 * g_theme.scale);
            break;

        case WidgetType::Separator:
            h = g_theme.spacing.MD;
            break;

        case WidgetType::Toggle:
            h = (int)(28 * g_theme.scale);
            break;

        case WidgetType::Slider:
            h = (int)(48 * g_theme.scale);  /* label row + track row */
            break;

        case WidgetType::KeyBadge:
        case WidgetType::KeyCapture:
            h = (int)(30 * g_theme.scale);
            break;

        case WidgetType::Label:
            h = (int)(20 * g_theme.scale);
            break;

        case WidgetType::MetaLabel:
            h = (int)(18 * g_theme.scale);
            break;

        case WidgetType::StatusRow:
            h = (int)(24 * g_theme.scale);
            break;

        case WidgetType::LinkButton:
            h = (int)(22 * g_theme.scale);
            break;

        case WidgetType::Spacer:
            h = w.spacerH;
            break;
        }

        w.bounds.bottom = w.bounds.top + h;
        y = w.bounds.bottom + g_theme.spacing.SM;
    }
}

/* ================================================================
 * Rendering — paint individual widget types
 * ================================================================ */

static void PaintSectionHeader(HDC hdc, const Widget& w, const Theme& t) {
    /* Section title */
    DrawTextLeft(hdc, t.fontSection, clr::Subtext1,
                 w.bounds.left, w.bounds.top,
                 w.bounds.right - w.bounds.left, (int)(18 * t.scale), w.label);

    /* Subtle separator line below */
    RECT lineRc = { w.bounds.left, w.bounds.bottom - 1,
                    w.bounds.right, w.bounds.bottom };
    FillSolidRect(hdc, lineRc, clr::Surface0);
}

static void PaintSeparator(HDC hdc, const Widget& w) {
    int midY = (w.bounds.top + w.bounds.bottom) / 2;
    RECT lineRc = { w.bounds.left, midY, w.bounds.right, midY + 1 };
    FillSolidRect(hdc, lineRc, clr::Surface0);
}

static void PaintToggle(HDC hdc, const Widget& w, const Theme& t, bool isFocused) {
    bool on = (w.boolVal && *w.boolVal);

    /* Label text */
    DrawTextLeft(hdc, t.fontBody, clr::Text,
                 w.bounds.left, w.bounds.top,
                 (int)(200 * t.scale), w.bounds.bottom - w.bounds.top, w.label);

    /* Toggle pill — 40x20 */
    int pillW = (int)(40 * t.scale), pillH = (int)(20 * t.scale);
    int pillX = w.bounds.right - pillW - g_theme.spacing.SM;
    int pillY = w.bounds.top + (w.bounds.bottom - w.bounds.top - pillH) / 2;
    RECT pillRc = { pillX, pillY, pillX + pillW, pillY + pillH };

    HBRUSH pillBr = on ? t.brAccent : t.brSurface0;
    FillRoundRect(hdc, pillRc, pillH, pillBr);

    if (!on) {
        /* Outline for off state */
        DrawRoundRectOutline(hdc, pillRc, pillH, clr::Surface1);
    }

    /* Thumb circle — 14px diameter */
    int thumbR = (int)(7 * t.scale);
    int thumbCY = pillY + pillH / 2;
    int thumbCX = on ? (pillX + pillW - thumbR - (int)(4 * t.scale)) : (pillX + thumbR + (int)(4 * t.scale));
    COLORREF thumbColor = on ? RGB(255, 255, 255) : clr::Surface2;
    DrawDot(hdc, thumbCX, thumbCY, thumbR, thumbColor);

    /* Focus ring */
    if (isFocused) {
        RECT focusRc = { pillX - 3, pillY - 3, pillX + pillW + 3, pillY + pillH + 3 };
        DrawRoundRectOutline(hdc, focusRc, pillH + 4, clr::Accent);
    }
}

static void PaintSlider(HDC hdc, const Widget& w, const Theme& t, bool isFocused) {
    int val = w.intVal ? *w.intVal : w.sliderMin;
    int trackY = w.bounds.top + (int)(24 * t.scale);  /* label occupies top 24px */
    int trackH = (int)(4 * t.scale);
    int trackL = w.bounds.left;
    int trackR = w.bounds.right - (int)(80 * t.scale);  /* reserve 80px for value text */
    int trackW = trackR - trackL;
    int trackMidY = trackY + trackH / 2;

    /* Label */
    DrawTextLeft(hdc, t.fontBody, clr::Subtext1,
                 w.bounds.left, w.bounds.top,
                 200, (int)(20 * t.scale), w.label);

    /* Value text — right aligned */
    wchar_t valBuf[32];
    if (w.sliderUnit && w.sliderUnit[0]) {
        _snwprintf(valBuf, 32, L"%d %s", val, w.sliderUnit);
    } else {
        _snwprintf(valBuf, 32, L"%d", val);
    }
    DrawTextRight(hdc, t.fontMono, clr::Subtext0,
                  trackR + g_theme.spacing.SM, trackY - 4,
                  72, trackH + 8, valBuf);

    /* Track background */
    RECT trackBgRc = { trackL, trackY, trackR, trackY + trackH };
    FillRoundRect(hdc, trackBgRc, trackH, t.brSurface0);

    /* Filled portion */
    float frac = 0.0f;
    if (w.sliderMax > w.sliderMin) {
        frac = (float)(val - w.sliderMin) / (float)(w.sliderMax - w.sliderMin);
    }
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    int fillW = (int)(frac * trackW);
    if (fillW > 0) {
        RECT fillRc = { trackL, trackY, trackL + fillW, trackY + trackH };
        FillRoundRect(hdc, fillRc, trackH, t.brAccent);
    }

    /* Default marker — small tick on track */
    if (w.sliderMax > w.sliderMin) {
        float defFrac = (float)(w.sliderDefault - w.sliderMin) /
                        (float)(w.sliderMax - w.sliderMin);
        int defX = trackL + (int)(defFrac * trackW);
        RECT defRc = { defX, trackY - 2, defX + 1, trackY + trackH + 2 };
        FillSolidRect(hdc, defRc, clr::Overlay0);
    }

    /* Thumb */
    int thumbR = (int)(((w.state == WState::Hover || w.state == WState::Press) ? 8 : 6) * t.scale);
    int thumbCX = trackL + fillW;
    COLORREF thumbClr = (w.state == WState::Press) ? clr::Accent : clr::Subtext1;
    if (w.state == WState::Hover) thumbClr = clr::Text;
    DrawDot(hdc, thumbCX, trackMidY, thumbR, thumbClr);

    /* Focus ring around entire slider area */
    if (isFocused) {
        RECT focusRc = { trackL - 4, trackY - 6, trackR + 4, trackY + trackH + 6 };
        DrawRoundRectOutline(hdc, focusRc, 6, clr::Accent);
    }
}

static void PaintKeyBadge(HDC hdc, const Widget& w, const Theme& t, bool isFocused) {
    /* Label: "Move Up" etc. */
    DrawTextLeft(hdc, t.fontBody, clr::Subtext1,
                 w.bounds.left, w.bounds.top,
                 (int)(150 * t.scale), w.bounds.bottom - w.bounds.top, w.label);

    /* Key cap badge */
    const wchar_t* keyName = w.valueText;
    if (!keyName[0]) keyName = L"?";

    /* Measure key text width */
    SIZE textSize = {0, 0};
    {
        HDC tempDC = hdc;  /* reuse */
        HFONT old = (HFONT)SelectObject(tempDC, t.fontBadge);
        GetTextExtentPoint32W(tempDC, keyName, (int)wcslen(keyName), &textSize);
        SelectObject(tempDC, old);
    }

    int badgeW = textSize.cx + g_theme.spacing.MD;
    int minW = (int)(36 * t.scale);
    if (badgeW < minW) badgeW = minW;  /* minimum width */
    int badgeH = (int)(22 * t.scale);
    int badgeX = w.bounds.left + (int)(120 * t.scale);
    int badgeY = w.bounds.top + (w.bounds.bottom - w.bounds.top - badgeH) / 2;
    RECT badgeRc = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };

    /* Badge background */
    FillRoundRect(hdc, badgeRc, 6, t.brSurface0);
    DrawRoundRectOutline(hdc, badgeRc, 6, clr::Surface1);

    /* Badge text */
    DrawTextLeft(hdc, t.fontBadge, clr::Text,
                 badgeX + g_theme.spacing.SM, badgeY,
                 badgeW - g_theme.spacing.MD, badgeH, keyName);

    /* "Remap" link */
    int remapX = badgeX + badgeW + g_theme.spacing.MD;
    COLORREF remapClr = (w.state == WState::Hover) ? clr::Accent : clr::Subtext0;
    DrawTextLeft(hdc, t.fontMeta, remapClr,
                 remapX, w.bounds.top,
                 (int)(60 * t.scale), w.bounds.bottom - w.bounds.top, L"Remap");

    /* Focus ring */
    if (isFocused) {
        RECT focusRc = { badgeX - 2, badgeY - 2, badgeX + badgeW + 2, badgeY + badgeH + 2 };
        DrawRoundRectOutline(hdc, focusRc, 8, clr::Accent);
    }
}

static void PaintKeyCapture(HDC hdc, const Widget& w, const Theme& t,
                             DWORD captureBlinkOn) {
    /* Label */
    DrawTextLeft(hdc, t.fontBody, clr::Subtext1,
                 w.bounds.left, w.bounds.top,
                 (int)(150 * t.scale), w.bounds.bottom - w.bounds.top, w.label);

    /* Capture badge — pulsing border */
    int badgeW = (int)(120 * t.scale);
    int badgeH = (int)(22 * t.scale);
    int badgeX = w.bounds.left + (int)(120 * t.scale);
    int badgeY = w.bounds.top + (w.bounds.bottom - w.bounds.top - badgeH) / 2;
    RECT badgeRc = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };

    FillRoundRect(hdc, badgeRc, 6, t.brSurface0);
    COLORREF borderClr = captureBlinkOn ? clr::Accent : clr::Surface1;
    DrawRoundRectOutline(hdc, badgeRc, 6, borderClr);

    /* "Press a key..." text */
    DrawTextLeft(hdc, t.fontMeta, clr::Overlay0,
                 badgeX + g_theme.spacing.SM, badgeY,
                 badgeW - g_theme.spacing.MD, badgeH, L"Press a key...");

    /* Countdown text */
    if (w.valueText[0]) {
        DrawTextLeft(hdc, t.fontMeta, clr::Overlay0,
                     badgeX, badgeY + badgeH + 2,
                     badgeW, (int)(14 * t.scale), w.valueText);
    }
}

static void PaintLabel(HDC hdc, const Widget& w, const Theme& t) {
    DrawTextLeft(hdc, t.fontBody, clr::Text,
                 w.bounds.left, w.bounds.top,
                 w.bounds.right - w.bounds.left,
                 w.bounds.bottom - w.bounds.top, w.label);
}

static void PaintMetaLabel(HDC hdc, const Widget& w, const Theme& t) {
    DrawTextLeft(hdc, t.fontMeta, clr::Overlay0,
                 w.bounds.left, w.bounds.top,
                 w.bounds.right - w.bounds.left,
                 w.bounds.bottom - w.bounds.top, w.label);
}

static void PaintStatusRow(HDC hdc, const Widget& w, const Theme& t) {
    /* Label */
    DrawTextLeft(hdc, t.fontBody, clr::Subtext0,
                 w.bounds.left, w.bounds.top,
                 (int)(150 * t.scale), w.bounds.bottom - w.bounds.top, w.label);

    /* Status dot */
    int dotX = w.bounds.left + (int)(130 * t.scale);
    int dotY = w.bounds.top + (w.bounds.bottom - w.bounds.top) / 2;
    if (w.statusColor == 1) {
        DrawDot(hdc, dotX, dotY, (int)(3 * t.scale), clr::Green);
    } else if (w.statusColor == 2) {
        DrawDot(hdc, dotX, dotY, (int)(3 * t.scale), clr::Red);
    }

    /* Value text in monospace */
    int valX = (w.statusColor > 0) ? dotX + (int)(12 * t.scale) : w.bounds.left + (int)(130 * t.scale);
    DrawTextLeft(hdc, t.fontMono, clr::Text,
                 valX, w.bounds.top,
                 w.bounds.right - valX,
                 w.bounds.bottom - w.bounds.top, w.valueText);
}

static void PaintLinkButton(HDC hdc, const Widget& w, const Theme& t, bool isFocused) {
    COLORREF clr_link = clr::Subtext0;
    if (w.state == WState::Hover) clr_link = clr::Accent;
    if (w.state == WState::Press) clr_link = clr::Text;

    DrawTextLeft(hdc, t.fontMeta, clr_link,
                 w.bounds.left, w.bounds.top,
                 w.bounds.right - w.bounds.left,
                 w.bounds.bottom - w.bounds.top, w.label);

    /* Focus underline */
    if (isFocused) {
        RECT ulRc = { w.bounds.left, w.bounds.bottom - 1,
                      w.bounds.left + 100, w.bounds.bottom };
        FillSolidRect(hdc, ulRc, clr::Accent);
    }
}

/* ================================================================
 * PaintWidget — dispatch to type-specific renderer
 * ================================================================ */
void PaintWidget(HDC hdc, const Widget& w, const Theme& t,
                 bool isFocused, DWORD captureBlinkOn) {
    if (!w.visible) return;

    switch (w.type) {
    case WidgetType::SectionHeader: PaintSectionHeader(hdc, w, t); break;
    case WidgetType::Separator:     PaintSeparator(hdc, w); break;
    case WidgetType::Toggle:        PaintToggle(hdc, w, t, isFocused); break;
    case WidgetType::Slider:        PaintSlider(hdc, w, t, isFocused); break;
    case WidgetType::KeyBadge:      PaintKeyBadge(hdc, w, t, isFocused); break;
    case WidgetType::KeyCapture:    PaintKeyCapture(hdc, w, t, captureBlinkOn); break;
    case WidgetType::Label:         PaintLabel(hdc, w, t); break;
    case WidgetType::MetaLabel:     PaintMetaLabel(hdc, w, t); break;
    case WidgetType::StatusRow:     PaintStatusRow(hdc, w, t); break;
    case WidgetType::LinkButton:    PaintLinkButton(hdc, w, t, isFocused); break;
    case WidgetType::Spacer:        break;  /* empty space, nothing to draw */
    }
}

/* ================================================================
 * Sidebar
 * ================================================================ */
static const wchar_t* s_tabLabels[] = {
    L"General", L"Controls", L"Motion", L"Diagnostics", L"About"
};
static const int s_tabCount = 5;

void PaintSidebar(HDC hdc, const RECT& sidebarRect,
                  int activeTab, int hoverTab, const Theme& t) {
    /* Background */
    FillRect(hdc, &sidebarRect, t.brCrust);

    

    /* Icon */
    int iconSize = (int)(24 * t.scale);
    int iconX = sidebarRect.left + t.spacing.MD;
    int iconY = sidebarRect.top + t.spacing.MD;
    HICON hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(101));
    if (hIcon) {
        DrawIconEx(hdc, iconX, iconY, hIcon, iconSize, iconSize, 0, NULL, DI_NORMAL);
    } else {
        RECT iconRc = { iconX, iconY, iconX + iconSize, iconY + iconSize };
        FillRoundRect(hdc, iconRc, (int)(6 * t.scale), t.brSurface0);
    }


    /* App title */
    DrawTextLeft(hdc, t.fontSection, clr::Subtext0,
                 iconX + iconSize + t.spacing.SM, iconY + (iconSize - (int)(20 * t.scale)) / 2,
                 t.spacing.SidebarW - t.spacing.MD * 2 - iconSize - t.spacing.SM, (int)(20 * t.scale), L"CursorMove");

    /* Tab items */
    int startY = sidebarRect.top + (int)(64 * t.scale);

    for (int i = 0; i < s_tabCount; ++i) {
        int iy = startY + i * g_theme.spacing.RowH;

        /* Hover background — very subtle */
        if (i == hoverTab && i != activeTab) {
            RECT hoverBg = { sidebarRect.left + 4, iy + 2,
                             sidebarRect.right - 4, iy + g_theme.spacing.RowH - 2 };
            FillRoundRect(hdc, hoverBg, 6, t.brSurface0);
        }

        /* Active indicator — lavender bar on left */
        if (i == activeTab) {
            int barH = 24;
            int barY = iy + (g_theme.spacing.RowH - barH) / 2;
            RECT barRc = { sidebarRect.left + 4, barY,
                           sidebarRect.left + 7, barY + barH };
            FillRoundRect(hdc, barRc, 3, t.brAccent);
        }

        /* Label */
        COLORREF textClr = clr::Subtext0;
        HFONT font = t.fontBody;
        if (i == activeTab) {
            textClr = clr::Text;
            font = t.fontSection;
        } else if (i == hoverTab) {
            textClr = clr::Subtext1;
        }
        DrawTextLeft(hdc, font, textClr,
                     sidebarRect.left + g_theme.spacing.MD + g_theme.spacing.SM, iy,
                     g_theme.spacing.SidebarW - g_theme.spacing.MD * 2, g_theme.spacing.RowH,
                     s_tabLabels[i]);
    }

    /* Version at bottom */
    DrawTextLeft(hdc, t.fontMeta, clr::Overlay0,
                 sidebarRect.left + g_theme.spacing.MD,
                 sidebarRect.bottom - 28,
                 g_theme.spacing.SidebarW - g_theme.spacing.MD * 2, 16, L"v1.0");

    /* Sidebar right edge — subtle separator */
    RECT edgeRc = { sidebarRect.right - 1, sidebarRect.top,
                    sidebarRect.right, sidebarRect.bottom };
    FillSolidRect(hdc, edgeRc, clr::Surface0);
}

/* ================================================================
 * Toast
 * ================================================================ */
void PaintToast(HDC hdc, const RECT& contentRect, const Theme& t,
                int fadeStep) {
    if (fadeStep >= 5) return;

    /* Blend toast alpha by shifting color toward background */
    int alpha = 255 - fadeStep * 50;
    if (alpha < 0) alpha = 0;

    /* We simulate alpha by blending RGB toward Base color */
    auto blend = [&](COLORREF fg, COLORREF bg) -> COLORREF {
        int r = (GetRValue(fg) * alpha + GetRValue(bg) * (255 - alpha)) / 255;
        int g = (GetGValue(fg) * alpha + GetGValue(bg) * (255 - alpha)) / 255;
        int b = (GetBValue(fg) * alpha + GetBValue(bg) * (255 - alpha)) / 255;
        return RGB(r, g, b);
    };

    int tw = 120, th = (int)(28 * g_theme.scale);
    int tx = contentRect.right - tw - g_theme.spacing.MD;
    int ty = contentRect.bottom - th - g_theme.spacing.MD;
    RECT toastRc = { tx, ty, tx + tw, ty + th };

    COLORREF bgClr = blend(clr::Surface0, clr::Base);
    COLORREF fgClr = blend(clr::Subtext1, clr::Base);

    HBRUSH toastBr = CreateSolidBrush(bgClr);
    FillRoundRect(hdc, toastRc, 6, toastBr);
    DeleteObject(toastBr);

    /* "Settings saved" */
    HFONT old = (HFONT)SelectObject(hdc, t.fontMeta);
    SetTextColor(hdc, fgClr);
    SetBkMode(hdc, TRANSPARENT);
    RECT textRc = toastRc;
    DrawTextW(hdc, L"Settings saved", -1, &textRc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(hdc, old);
}

/* ================================================================
 * Hit testing
 * ================================================================ */
int HitTestPanel(const Panel& panel, int x, int y) {
    for (int i = 0; i < panel.count; ++i) {
        const Widget& w = panel.widgets[i];
        if (!w.visible) continue;
        if (w.type == WidgetType::SectionHeader ||
            w.type == WidgetType::Separator ||
            w.type == WidgetType::Spacer ||
            w.type == WidgetType::MetaLabel ||
            w.type == WidgetType::Label) {
            continue;  /* non-interactive */
        }
        POINT pt = { x, y };
        if (PtInRect(&w.bounds, pt)) {
            return i;
        }
    }
    return -1;
}

int HitTestSidebar(const RECT& sidebarRect, int x, int y) {
    if (x < sidebarRect.left || x >= sidebarRect.right) return -1;
    int startY = sidebarRect.top + (int)(64 * g_theme.scale);
    for (int i = 0; i < s_tabCount; ++i) {
        int iy = startY + i * g_theme.spacing.RowH;
        if (y >= iy && y < iy + g_theme.spacing.RowH) {
            return i;
        }
    }
    return -1;
}

int SliderValueFromX(const Widget& w, int x) {
    int trackL = w.bounds.left;
    int trackR = w.bounds.right - (int)(80 * g_theme.scale);
    int trackW = trackR - trackL;
    if (trackW <= 0) return w.sliderMin;

    float frac = (float)(x - trackL) / (float)trackW;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    return w.sliderMin + (int)(frac * (w.sliderMax - w.sliderMin) + 0.5f);
}

int SliderThumbX(const Widget& w) {
    int val = w.intVal ? *w.intVal : w.sliderMin;
    int trackL = w.bounds.left;
    int trackR = w.bounds.right - (int)(80 * g_theme.scale);
    int trackW = trackR - trackL;

    float frac = 0.0f;
    if (w.sliderMax > w.sliderMin) {
        frac = (float)(val - w.sliderMin) / (float)(w.sliderMax - w.sliderMin);
    }
    return trackL + (int)(frac * trackW);
}

bool SliderHitThumb(const Widget& w, int x, int y) {
    int thumbCX = SliderThumbX(w);
    int trackY = w.bounds.top + (int)(24 * g_theme.scale);
    int trackH = (int)(4 * g_theme.scale);
    int thumbCY = trackY + trackH / 2;
    int dx = x - thumbCX;
    int dy = y - thumbCY;
    return (dx * dx + dy * dy) <= (int)(144 * g_theme.scale * g_theme.scale);  /* generous hit area */
}

bool KeyBadgeHitRemap(const Widget& w, int x, int y) {
    /* "Remap" link is positioned after the badge */
    const wchar_t* keyName = w.valueText;
    if (!keyName[0]) keyName = L"?";

    /* Estimate badge width (we don't have HDC here, use rough calc) */
    int nameLen = (int)wcslen(keyName);
    int badgeW = nameLen * (int)(8 * g_theme.scale) + g_theme.spacing.MD;
    int minW = (int)(36 * g_theme.scale);
    if (badgeW < minW) badgeW = minW;

    int remapX = w.bounds.left + (int)(120 * g_theme.scale) + badgeW + g_theme.spacing.MD;
    int remapRight = remapX + (int)(50 * g_theme.scale);
    return (x >= remapX && x <= remapRight &&
            y >= w.bounds.top && y < w.bounds.bottom);
}

} /* namespace cm */
