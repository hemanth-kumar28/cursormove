# CursorMove Settings UI Redesign

A comprehensive redesign of the settings interface targeting modern Windows utility aesthetics, improved usability, accessibility, and production-grade reliability — all within native Win32/C++14/MinGW constraints.

## Current State Analysis

### Architecture Overview
- **Pure Win32** app: 4-tab settings window (General, Keys, Motion, Diagnostics) using custom `WndProc` panels parented to a tab control
- **Dark theme**: Hard-coded `RGB(30,30,46)` bg / `RGB(42,42,62)` panel / `RGB(205,214,244)` text / `RGB(137,180,250)` accent
- **Fixed dimensions**: `485×410` pixels, non-resizable (`WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU`)
- **Controls**: System BUTTON, STATIC, TRACKBAR classes with owner-drawn color handling
- **Remap workflow**: `MessageBox` + 3-second busy polling loop (blocks UI thread)

### Critical UX Problems Identified

| # | Problem | Impact |
|---|---------|--------|
| 1 | **Wide, squat layout** (485×410) wastes horizontal space, feels legacy | Poor first impression |
| 2 | **No DPI scaling** — all pixel positions are hard-coded absolute values | Broken on 125%/150%/200% DPI monitors |
| 3 | **System font (Tahoma/MS Shell Dlg)** is small, low-contrast, dated | Poor readability and modern feel |
| 4 | **No section grouping** — controls float without visual separation | High cognitive load |
| 5 | **Slider labels cramped** — 90px labels truncated, values right-aligned with no padding | Misalignment, hard to scan |
| 6 | **Remap dialog blocks UI** thread with MessageBox + busy-wait polling | Freezes entire settings window |
| 7 | **No hover/focus feedback** — buttons and checkboxes are plain system controls | Feels flat and unresponsive |
| 8 | **Diagnostics are plain text** — no status indicators (colors/icons) for Active/Inactive | Hard to quickly assess system health |
| 9 | **Tab control uses system theme** — doesn't match dark background | Visual inconsistency |
| 10 | **No tooltips** on any control — new users have no clue what "Precision %" or "Smoothing %" means | Poor discoverability |
| 11 | **"Apply & Save" button** only on Motion tab — General tab changes save immediately, inconsistent | Confusing save model |
| 12 | **No master enable/disable indicator** visible across all tabs | User can't tell if system is active |
| 13 | **Window has no icon** — uses `IDI_APPLICATION` (generic Windows icon) | Unprofessional |
| 14 | **No minimum size enforcement** — WS_OVERLAPPED allows no resize, but if changed, no safeguards | Potential layout breakage |
| 15 | **GDI brush cleanup only on Destroy()** — if Init() called twice, leaks brushes | Resource leak |

---

## Proposed Changes

### Component 1: Window Dimensions & Layout Engine

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**New window dimensions:** `380×560` (compact width, taller for vertical flow)

**DPI-aware layout system:**
- Add `ScaleDPI(int px)` helper that reads system DPI via `GetDpiForWindow()` (Win10+) or `GetDeviceCaps(LOGPIXELSX)` fallback
- All absolute pixel values replaced with `Dpi(px)` macro calls
- Layout constants defined once at top, scaled at runtime

```cpp
// New layout constants (base 96 DPI values, scaled at runtime)
static int DPI = 96;
static int Scale(int v) { return MulDiv(v, DPI, 96); }

// Window dimensions (96 DPI base)
static const int BASE_W = 380;
static const int BASE_H = 560;
```

**Vertical content layout** — each tab panel uses a simple `y += rowHeight` accumulator pattern with consistent 12px/16px vertical padding between groups.

---

### Component 2: Color Palette & Visual System

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Refined dark theme palette** — better contrast ratios (WCAG AA compliant), more visual depth:

```cpp
// Background layers (depth through luminance)
static const COLORREF CLR_BG        = RGB(24, 24, 37);   // Deepest background
static const COLORREF CLR_SURFACE   = RGB(35, 35, 52);   // Panel/card surface  
static const COLORREF CLR_SURFACE2  = RGB(45, 45, 65);   // Elevated surface (tab bar, header)
static const COLORREF CLR_BORDER    = RGB(58, 58, 82);   // Subtle borders

// Text hierarchy
static const COLORREF CLR_TEXT      = RGB(210, 218, 245); // Primary text (≥7:1 contrast)
static const COLORREF CLR_TEXT_SEC  = RGB(150, 158, 185); // Secondary/hint text
static const COLORREF CLR_TEXT_DIM  = RGB(100, 108, 140); // Disabled/muted text

// Semantic colors
static const COLORREF CLR_ACCENT    = RGB(130, 170, 255); // Primary accent (links, active)
static const COLORREF CLR_SUCCESS   = RGB(100, 220, 140); // Active/healthy status
static const COLORREF CLR_WARNING   = RGB(250, 190, 70);  // Warning state
static const COLORREF CLR_DANGER    = RGB(240, 100, 100);  // Error/stopped state
static const COLORREF CLR_DISABLED  = RGB(70, 70, 95);    // Disabled control bg
```

Additional GDI brushes created in `Init()` for each surface level.

---

### Component 3: Typography

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Custom fonts** via `CreateFontW()` — Segoe UI (standard on Win7+), with proper hierarchy:

```cpp
static HFONT s_fontHeading  = NULL;  // 15px, Semibold (FW_SEMIBOLD)
static HFONT s_fontBody     = NULL;  // 13px, Regular (FW_NORMAL)
static HFONT s_fontSmall    = NULL;  // 11px, Regular — for hints/values
static HFONT s_fontMono     = NULL;  // 12px, Consolas — for key names & diagnostics
```

All controls receive `WM_SETFONT` after creation. Font sizes are DPI-scaled.

---

### Component 4: Master Status Header

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Persistent header bar** above the tab control — always visible across all tabs:

```
┌──────────────────────────────────────┐
│  ● CursorMove    [Enabled ▼]        │  ← Status dot (green/red) + toggle
│  Alt+S toggle · Ctrl+Alt+Esc panic  │  ← Hotkey reminder (dim text)
├──────────────────────────────────────┤
│  General │ Keys │ Motion │ Diag     │  ← Tab bar
│  ─────────────────────────────────── │
│         (tab content area)          │
```

- Painted as a custom header in `WM_PAINT` of the main window
- Status dot: filled circle drawn via `Ellipse()` — green when enabled, red when disabled
- Updated via the 500ms diagnostics timer (already exists)

---

### Component 5: Tab Bar Redesign

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Owner-drawn tab control** (`TCS_OWNERDRAWFIXED`) for dark theme consistency:

- `WM_DRAWITEM` handler paints each tab with dark background, accent underline for selected tab
- Selected tab: `CLR_SURFACE2` background with 2px `CLR_ACCENT` bottom border
- Unselected: `CLR_BG` background, `CLR_TEXT_SEC` text
- Hover: `CLR_SURFACE` background (via `WM_MOUSEMOVE` tracking)

---

### Component 6: General Tab Improvements

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Grouped sections** with subtle divider lines:

```
╔══ System ═══════════════════════════╗
║  [✓] Start with Windows            ║
║  [✓] Swallow keyboard input        ║
║      ↳ Block keys from other apps  ║  ← hint text (CLR_TEXT_SEC, small font)
╚═════════════════════════════════════╝

╔══ Hotkeys ══════════════════════════╗
║  Toggle    Alt+S                    ║
║  Panic     Ctrl+Alt+Esc            ║
╚═════════════════════════════════════╝
```

- Section headers: `s_fontHeading`, accent color, with a horizontal line extending to the right
- Sub-hints under complex options (e.g., "Block keys from other apps" under swallow checkbox)
- Enabled/disabled checkbox removed from General — now in header

---

### Component 7: Keys Tab Improvements

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Grouped by function** with visual separation:

```
── Movement ──────────────────────────
  Up         W        [Remap]
  Down       S        [Remap]
  Left       A        [Remap]
  Right      D        [Remap]

── Clicks ────────────────────────────
  Left       Space    [Remap]
  Right      E        [Remap]
  Middle     Q        [Remap]

── Scroll ────────────────────────────
  Up         R        [Remap]
  Down       F        [Remap]
```

- Key name labels use `s_fontMono` (Consolas) for consistent width
- Remap buttons use accent border on hover
- Current key shown in a sunken-frame label with mono font

**Improved remap workflow:**
- Instead of MessageBox + blocking poll, use a **modeless capture state**:
  1. Click "Remap" → button text changes to "Press a key..." with pulsing accent color
  2. Set a flag + install a temporary keyboard hook (or reuse existing)
  3. Next keypress is captured → label updates → state resets
  4. Escape cancels, 5-second timeout auto-cancels
  5. **Non-blocking** — doesn't freeze the UI thread

---

### Component 8: Motion Tab Improvements

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Better slider layout** with value inline:

```
── Speed ─────────────────────────────
  Base speed                 180 px/s
  ├─────────────●───────────────────┤

  Max speed                 1200 px/s
  ├────────────────────●────────────┤

── Acceleration ──────────────────────
  Accel time                  250 ms
  ├───────●─────────────────────────┤

  Decel time                  140 ms
  ├────●────────────────────────────┤

── Fine Tuning ───────────────────────
  Precision                      35%
  ├─────●───────────────────────────┤
  ℹ Speed multiplier in precision mode

  Smoothing                      15%
  ├──●──────────────────────────────┤

  Tick rate                    90 Hz
  ├──────────────●──────────────────┤

  Scroll speed               600 u/s
  ├────────────●────────────────────┤

  [ Apply & Save ]  [ Reset Defaults ]
```

- Label on left, value on right (same line), slider below — takes more vertical space but much clearer
- Value label updates in real-time via `WM_HSCROLL`
- Tooltips on labels explaining each parameter
- "Reset Defaults" button added next to "Apply & Save"
- Slider groups: Speed, Acceleration, Fine Tuning

---

### Component 9: Diagnostics Tab Improvements

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Status cards with colored indicators:**

```
── System Status ─────────────────────
  ● Hook         Active            ← green dot
  ● Motion       Running           ← green dot  
  ○ Last Error   None              ← gray dot (no error)

── Performance ───────────────────────
  Tick Rate      90 Hz
  CPU Load       2%                ← green if <10%, yellow <25%, red >25%

── Mode ──────────────────────────────
  Current Mode   Normal            
  Enabled        Yes               ← green "Yes" / red "No"
```

- Status dots: `●` painted via `Ellipse()` with semantic colors
- Performance metrics: color-coded based on health thresholds
- Auto-refresh continues via existing 500ms timer

---

### Component 10: Tooltip System

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

**Centralized tooltip creation** using `TOOLTIPS_CLASS`:

```cpp
static HWND s_tooltip = NULL;

static void AddTooltip(HWND parent, HWND control, const wchar_t* text) {
    TOOLINFOW ti = {};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
    ti.hwnd = parent;
    ti.uId = (UINT_PTR)control;
    ti.lpszText = (LPWSTR)text;
    SendMessage(s_tooltip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
}
```

Tooltips added to:
- All sliders (explaining what the parameter does)
- Swallow checkbox ("When enabled, bound keys won't reach other applications")
- Remap buttons ("Click to assign a new key")
- Status indicators ("Shows whether the keyboard hook thread is running")

---

### Component 11: Keyboard Navigation & Accessibility

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

- All controls created with `WS_TABSTOP` where appropriate
- Tab order follows visual top-to-bottom, left-to-right flow
- `WS_GROUP` on first control in each section
- Focus rectangle handled by system (already works with `WS_TABSTOP`)
- `IsDialogMessage()` added to settings window's message processing for proper Tab/Shift+Tab handling

---

### Component 12: Tray Menu Consistency

#### [MODIFY] [tray.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/tray.cpp)

- Add `MF_GRAYED` state for Settings when window already open
- Add separator and version string at bottom (non-clickable `MF_GRAYED`)
- Show current mode in the "Enabled" item: "Enabled (Normal)" / "Enabled (Precision)"

---

### Component 13: Resource Cleanup & Stability

#### [MODIFY] [ui.cpp](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/src/ui.cpp)

- Guard `Init()` against double-call (check if brushes already exist)
- Add font cleanup in `Destroy()` — delete all 4 font handles
- Add tooltip window destruction in `Destroy()`
- WM_DPICHANGED handler to recreate fonts and resize window when DPI changes at runtime
- Double-buffer the header paint via `BeginBufferedPaint` or manual back buffer to prevent flicker

---

### Component 14: Manifest Update for Per-Monitor DPI

#### [MODIFY] [app.manifest](file:///c:/Users/khema/Desktop/PIP/zbasic%20self/projis/cursormove/res/app.manifest)

Update DPI awareness to Per-Monitor V2 (falls back gracefully on older Windows):

```xml
<dpiAware xmlns="http://schemas.microsoft.com/SMI/2005/WindowsSettings">true/pm</dpiAware>
<dpiAwareness xmlns="http://schemas.microsoft.com/SMI/2016/WindowsSettings">PerMonitorV2,PerMonitor</dpiAwareness>
```

---

## User Review Required

> [!IMPORTANT]
> **Window Dimensions Change**: The proposed shift from `485×410` (wide) to `380×560` (tall/narrow) is a significant layout change. This creates a more mobile-inspired, focused panel feel. If you prefer a different aspect ratio or specific dimensions, please specify.

> [!IMPORTANT]
> **Remap Workflow Redesign**: The current `MessageBox + 3-second busy poll` will be replaced with a non-blocking inline capture approach. This is a behavioral change — the button becomes the capture target instead of a separate dialog. The existing hook thread is NOT reused for this (to avoid complexity); instead we use `GetAsyncKeyState` polling on a short timer (50ms) that doesn't block the UI.

> [!WARNING]
> **Owner-drawn tab control**: Implementing `TCS_OWNERDRAWFIXED` tabs adds ~80 lines of drawing code but gives full dark-theme consistency. The alternative is to keep the system tab control (which will render with the light Windows theme, creating a visual mismatch). I recommend owner-drawn.

## Open Questions

> [!IMPORTANT]
> **Slider layout**: The proposed layout puts the label+value on one line and slider on the next line below. This doubles vertical space per slider but is much more readable. The Motion tab will become scrollable or we can keep the current inline layout if vertical space is a concern. Which do you prefer?
> - **Option A**: Label+value above, slider below (clearer but taller — may need scroll or smaller groups)
> - **Option B**: Label left, slider center, value right on same line (compact, current approach but polished)

> [!NOTE]
> **Font choice**: Segoe UI is available on all Windows 7+ systems. If you need Windows XP support, we fall back to "Tahoma" automatically. Consolas for monospace is also universal on Win7+.

---

## Verification Plan

### Build Verification
```bash
build.bat         # Must compile clean with MinGW g++ 6.3 / C++14
build.bat debug   # Debug build for testing
```

### Automated Checks
- [ ] Build succeeds with zero warnings (existing `-Wall -Wextra` flags)
- [ ] Window opens at correct DPI-scaled dimensions
- [ ] All 4 tabs switch correctly with proper panel visibility
- [ ] Slider values update in real-time during drag
- [ ] Key remap capture works without freezing UI
- [ ] Diagnostics update every 500ms
- [ ] Enable/disable toggle reflects in header immediately
- [ ] Tooltips appear on hover with correct text
- [ ] Tab key navigates through all controls in correct order
- [ ] Window close hides (doesn't destroy), re-open restores state
- [ ] GDI resource count stable after repeated open/close cycles
- [ ] No flicker on tab switch or window redraw

### Edge Cases to Validate
- [ ] 100%, 125%, 150%, 200% DPI scaling
- [ ] Rapid tab switching (10+ times in 1 second)
- [ ] Open/close settings 10+ times consecutively
- [ ] Remap capture with timeout (wait 5 seconds without pressing)
- [ ] Remap capture with Escape cancel
- [ ] Long diagnostic strings don't overflow labels
- [ ] Tray menu works correctly while settings is open
- [ ] Window position restores after tray restore

### Manual Verification
- [ ] Visual inspection of all 4 tabs for alignment and spacing
- [ ] Color contrast verification (text readable on all surfaces)
- [ ] Font rendering quality at different DPI levels
