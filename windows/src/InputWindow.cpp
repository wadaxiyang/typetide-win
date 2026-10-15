#include "InputWindow.h"
#include "FeatureControls.h"
#include "Translator.h"
#include "Clipboard.h"
#include "UITheme.h"
#include "BrandMark.h"
#include "resource.h"
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <algorithm>
#include <array>
#include <windowsx.h>

namespace {
constexpr wchar_t kClass[] = L"TypeTide-WinInput";
enum { Source = 2000, Swap, Target, Input, Run, Output, Status, Hint, Copy, Close };
struct Done { bool ok; std::wstring error; };
struct State {
    HWND hwnd = nullptr, app = nullptr;
    HFONT font = nullptr, smallFont = nullptr, headerFont = nullptr;
    HWND tooltip = nullptr, hovered = nullptr;
    UINT dpi = 96;
    FeatureConfig config, defaults;
    std::wstring settingsContext;
    uint64_t request = 0;
    bool busy = false, loading = false, composing = false, dirty = false;
    bool dark = false, contrast = false;
    std::wstring result;
    std::array<int, 2> wheelRemainder{};
    ui::SolidBrush surface;
} g;
HWND control(int id) { return GetDlgItem(g.hwnd, id); }
int px(int n) { return MulDiv(n, g.dpi, 96); }
ui::Palette palette() { return ui::palette(g.dark, g.contrast); }
void status(const std::wstring& text) { SetWindowTextW(control(Status), text.c_str()); }
void stop() {
    if (g.request) translator::Cancel(g.request);
    g.request = 0;
    g.busy = false;
    SetWindowTextW(control(Run), L"Translate");
}
void stale() {
    if (g.loading) return;
    if (g.busy) stop();
    if (!g.result.empty()) { g.dirty = true; status(L"Input or options changed — translate again."); }
    else status(L"");
    EnableWindow(control(Copy), !g.result.empty() && !g.dirty);
}
void configureControls() {
    g.loading = true;
    const auto& settings = Settings::shared();
    auto label = [&](int value, bool source) {
        if (value == kLanguageNative) return lang::ShortName(settings.nativeLanguage);
        if (value == kLanguageForeign) return lang::ShortName(settings.foreignLanguage);
        return settings.languageLabel(value, source);
    };
    SetWindowTextW(control(Source), (label(g.config.source, true) + L"  ▾").c_str());
    SetWindowTextW(control(Target), (label(g.config.target, false) + L"  ▾").c_str());
    g.loading = false;
}
void syncSettings() {
    const auto& settings = Settings::shared();
    const auto latest = settings.feature(Feature::Input);
    const auto old = g.config;
    if (g.config.source == g.defaults.source) g.config.source = latest.source;
    if (g.config.target == g.defaults.target) g.config.target = latest.target;
    g.config.providerId = latest.providerId;
    g.config.prompt = latest.prompt; g.config.style = latest.style;
    g.defaults = latest;
    const auto context = settings.providerSummary(g.config) + settings.languageLabel(kLanguageNative) +
        settings.languageLabel(kLanguageForeign);
    if (!(g.config == old) || context != g.settingsContext) { configureControls(); stale(); }
    g.settingsContext = context;
}
void translate() {
    if (g.busy) { stop(); status(L"Stopped. Your input is preserved."); return; }
    auto text = util::Trim(featureui::text(control(Input)));
    if (text.empty()) { status(L"Enter some text to translate."); SetFocus(control(Input)); return; }
    if (text.size() > 5000) { status(L"Please use 5,000 characters or fewer."); return; }
    syncSettings();
    const auto& settings = Settings::shared();
    const auto route = settings.resolveFeature(text, Feature::Input, g.config);
    const std::wstring direction = (route.source ? lang::ShortName(*route.source) : L"Auto detect") +
        L" → " + (g.config.target == kLanguageSame ? L"original language" : lang::ShortName(route.target));
    status(L"Translating · " + direction);
    g.result.clear(); g.dirty = false; g.busy = true;
    SetWindowTextW(control(Output), L"");
    EnableWindow(control(Copy), FALSE);
    SetWindowTextW(control(Run), L"Stop");
    const auto app = g.app;
    g.request = translator::StreamWithProviders(
        {text, route.source, route.target, g.config.style, g.config.prompt, g.config.target == kLanguageSame},
        settings.providersFor(g.config),
        [app](uint64_t id, const std::wstring& delta) {
            auto* value = new std::wstring(delta);
            if (!PostMessageW(app, WM_APP_INPUT_DELTA, (WPARAM)id, (LPARAM)value)) delete value;
        },
        [app](uint64_t id, bool ok, const std::wstring& error) {
            auto* value = new Done{ok, error};
            if (!PostMessageW(app, WM_APP_INPUT_DONE, (WPARAM)id, (LPARAM)value)) delete value;
        });
}
void swapLanguages() {
    const auto& settings = Settings::shared();
    const auto route = settings.resolveFeature(featureui::text(control(Input)), Feature::Input, g.config);
    if (!route.source) { status(L"Choose a source language before swapping."); return; }
    g.config.source = (int)route.target;
    g.config.target = (int)*route.source;
    // When a complete result exists, reverse the translation using that text.
    if (!g.result.empty() && !g.dirty && !g.busy) {
        g.loading = true;
        SetWindowTextW(control(Input), g.result.c_str());
        g.loading = false;
    }
    configureControls(); stale();
}
void layout() {
    RECT r{}; GetClientRect(g.hwnd, &r);
    const int w = MulDiv(r.right, 96, g.dpi), h = MulDiv(r.bottom, 96, g.dpi);
    const int margin = 20, content = w - margin * 2;
    auto move = [&](int id, int x, int y, int width, int height) {
        SetWindowPos(control(id), nullptr, px(x), px(y), px(width), px(height), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    move(Close, w - margin - 32, 11, 32, 32);
    const int editorHeight = std::max(40, (h - 260) / 2);
    move(Source, margin, 66, 154, 28);
    move(Input, margin, 106, content, editorHeight);
    move(Target, margin, 130 + editorHeight, 154, 28);
    move(Output, margin, 170 + editorHeight, content, editorHeight);
    move(Status, margin, h - 82, content, 18);
    move(Hint, margin, h - 54, content - 188, 34);
    move(Swap, w - margin - 178, h - 54, 32, 34);
    move(Copy, w - margin - 138, h - 54, 32, 34);
    move(Run, w - margin - 94, h - 54, 94, 34);
}
void theme() {
    g.dark = Settings::shared().useDarkMode(); g.contrast = ui::highContrastEnabled();
    BOOL dark = g.dark && !g.contrast;
    DwmSetWindowAttribute(g.hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    const DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(g.hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
    const BOOL disableTransitions = TRUE;
    DwmSetWindowAttribute(g.hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disableTransitions, sizeof(disableTransitions));
    EnumChildWindows(g.hwnd, [](HWND child, LPARAM dark) -> BOOL {
        wchar_t name[32]{}; GetClassNameW(child, name, 32);
        const bool field = _wcsicmp(name, L"Edit") == 0 || _wcsicmp(name, L"ComboBox") == 0;
        SetWindowTheme(child, dark ? (field ? L"DarkMode_CFD" : L"DarkMode_Explorer") : L"Explorer", nullptr);
        return TRUE;
    }, dark);
    RedrawWindow(g.hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
}
void font() {
    const std::array old{g.font, g.smallFont, g.headerFont};
    auto make = [](int size, int weight) {
        return CreateFontW(-MulDiv(size, g.dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    };
    g.font = make(12, FW_NORMAL); g.smallFont = make(9, FW_NORMAL); g.headerFont = make(9, FW_SEMIBOLD);
    EnumChildWindows(g.hwnd, [](HWND child, LPARAM) -> BOOL {
        const bool editor = child == control(Input) || child == control(Output);
        SendMessageW(child, WM_SETFONT, (WPARAM)(editor ? g.font : g.smallFont), TRUE); return TRUE;
    }, 0);
    for (auto f : old) if (f) DeleteObject(f);
}
LRESULT CALLBACK inputSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_IME_STARTCOMPOSITION) g.composing = true;
    if (msg == WM_IME_ENDCOMPOSITION) g.composing = false;
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, inputSubclass, 1);
    if (msg == WM_SETTEXT) {
        const auto result = DefSubclassProc(hwnd, msg, wp, lp);
        // Multiline edits do not emit EN_CHANGE for WM_SETTEXT (swap, clear, accessibility).
        g.wheelRemainder[hwnd == control(Input) ? 0 : 1] = 0;
        if (hwnd == control(Input)) stale();
        return result;
    }
    if (msg == WM_MOUSEWHEEL) {
        // Keep native caret/keyboard scrolling, but scroll by wheel without a visible scrollbar.
        auto& remainder = g.wheelRemainder[hwnd == control(Input) ? 0 : 1];
        remainder += GET_WHEEL_DELTA_WPARAM(wp);
        const int steps = remainder / WHEEL_DELTA;
        remainder %= WHEEL_DELTA;
        UINT lines = 3; SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
        if (lines == WHEEL_PAGESCROLL) {
            RECT r{}; GetClientRect(hwnd, &r);
            HDC dc = GetDC(hwnd); auto old = SelectObject(dc, g.font);
            TEXTMETRICW metrics{}; GetTextMetricsW(dc, &metrics);
            SelectObject(dc, old); ReleaseDC(hwnd, dc);
            lines = std::max(1L, r.bottom / std::max(1L, metrics.tmHeight));
        }
        SendMessageW(hwnd, EM_LINESCROLL, 0, -steps * (int)lines);
        return 0;
    }
    if (msg == WM_PAINT) {
        const auto result = DefSubclassProc(hwnd, msg, wp, lp);
        if (GetWindowTextLengthW(hwnd) == 0) {
            HDC dc = GetDC(hwnd); RECT r{}; SendMessageW(hwnd, EM_GETRECT, 0, (LPARAM)&r);
            auto old = SelectObject(dc, g.font); SetBkMode(dc, TRANSPARENT);
            const auto p = palette();
            const auto muted = g.contrast ? p.secondary : RGB((GetRValue(p.secondary) + GetRValue(p.surface)) / 2,
                (GetGValue(p.secondary) + GetGValue(p.surface)) / 2, (GetBValue(p.secondary) + GetBValue(p.surface)) / 2);
            SetTextColor(dc, muted);
            const wchar_t* hint = hwnd == control(Input) ? L"Input here" : g.busy ? L"Translating…" : L"Translation appears here";
            DrawTextW(dc, hint, -1, &r, DT_WORDBREAK | DT_NOPREFIX);
            SelectObject(dc, old); ReleaseDC(hwnd, dc);
        }
        return result;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
LRESULT CALLBACK buttonSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR) {
    if (msg == WM_MOUSEMOVE && g.hovered != hwnd) {
        auto previous = g.hovered; g.hovered = hwnd;
        if (previous) InvalidateRect(previous, nullptr, FALSE);
        InvalidateRect(hwnd, nullptr, FALSE);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0}; TrackMouseEvent(&track);
    }
    if (msg == WM_MOUSELEAVE) {
        if (g.hovered == hwnd) g.hovered = nullptr;
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, buttonSubclass, 1);
    return DefSubclassProc(hwnd, msg, wp, lp);
}
void chooseOption(int id) {
    const auto& settings = Settings::shared();
    HMENU menu = CreatePopupMenu();
    if (!menu) return;
    std::vector<int> languages{kLanguageAuto, kLanguageNative, kLanguageForeign};
    if (id == Target) languages.push_back(kLanguageSame);
    for (auto language : lang::All) languages.push_back((int)language);
    for (size_t i = 0; i < languages.size(); ++i) {
        const auto value = languages[i];
        AppendMenuW(menu, MF_STRING | (value == (id == Source ? g.config.source : g.config.target) ? MF_CHECKED : 0),
            i + 1, settings.languageLabel(value, id == Source).c_str());
    }
    RECT at{}; GetWindowRect(control(id), &at);
    const UINT selected = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_LEFTALIGN | TPM_TOPALIGN,
        at.left, at.bottom, 0, g.hwnd, nullptr);
    DestroyMenu(menu);
    if (!selected) return;
    if (selected - 1 < languages.size()) {
        (id == Source ? g.config.source : g.config.target) = languages[selected - 1];
    }
    configureControls(); stale();
}
void paint(HDC dc) {
    RECT r{}; GetClientRect(g.hwnd, &r);
    const auto p = palette();
    FillRect(dc, &r, g.surface.get(p.surface));
    ui::SolidBrush divider;
    FrameRect(dc, &r, divider.get(p.divider));
    const RECT mark{px(20), px(10), px(52), px(42)};
    brand::DrawMark(dc, mark, p.text, !g.contrast);
    auto oldFont = SelectObject(dc, g.headerFont);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, p.secondary);
    RECT title{px(59), 0, r.right - px(64), px(54)};
    DrawTextW(dc, L"TypeTide-Win", -1, &title, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
    RECT line{px(20), px(54), r.right - px(20), px(54) + 1};
    FillRect(dc, &line, divider.get(p.divider));
    RECT editor{}; GetWindowRect(control(Input), &editor); MapWindowPoints(nullptr, g.hwnd, (POINT*)&editor, 2);
    line.top = editor.bottom + px(12); line.bottom = line.top + 1;
    FillRect(dc, &line, divider.get(p.divider));
    SelectObject(dc, oldFont);
}
void drawButton(const DRAWITEMSTRUCT& item) {
    const auto p = palette();
    const int id = item.CtlID;
    const bool enabled = (item.itemState & ODS_DISABLED) == 0;
    const bool primary = id == Run && enabled;
    const bool hovered = g.hovered == item.hwndItem || (item.itemState & ODS_SELECTED);
    const bool flat = id == Close || id == Swap || id == Copy;
    const bool chip = id == Source || id == Target;
    COLORREF fill = primary ? (g.contrast ? GetSysColor(COLOR_HIGHLIGHT) : ui::Accent) :
        flat && !hovered ? p.surface : chip && !hovered ? p.background : p.button;
    if (hovered && primary && !g.contrast)
        fill = RGB(std::min(255, GetRValue(fill) + 20), std::min(255, GetGValue(fill) + 20), std::min(255, GetBValue(fill) + 20));
    FillRect(item.hDC, &item.rcItem, g.surface.get(p.surface));
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, primary || (flat && !hovered) ? fill : chip && !hovered ? p.surface : p.buttonBorder);
    auto oldBrush = SelectObject(item.hDC, brush); auto oldPen = SelectObject(item.hDC, pen);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, px(8), px(8));
    SelectObject(item.hDC, oldPen); SelectObject(item.hDC, oldBrush); DeleteObject(pen); DeleteObject(brush);
    auto oldFont = SelectObject(item.hDC, g.smallFont);
    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, !enabled ? p.secondary : primary ?
        (g.contrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ui::AccentText) : flat || chip ? p.secondary : p.text);
    auto r = item.rcItem; InflateRect(&r, -px(6), 0);
    if (id == Copy || id == Swap) {
        const int side = px(18), x = (item.rcItem.left + item.rcItem.right - side) / 2;
        const int y = (item.rcItem.top + item.rcItem.bottom - side) / 2;
        const bool copied = id == Copy && featureui::text(item.hwndItem) == L"Copied";
        const auto action = id == Swap ? vectoricons::Action::Exchange : copied ? vectoricons::Action::Check : vectoricons::Action::Copy;
        const auto color = enabled ? (copied ? ui::accent(g.dark, g.contrast) : p.secondary) :
            g.contrast ? p.secondary : p.buttonBorder;
        vectoricons::DrawAction(item.hDC, {x, y, x + side, y + side}, action, color);
    } else {
        auto label = id == Close ? std::wstring(L"✕") : featureui::text(item.hwndItem);
        DrawTextW(item.hDC, label.c_str(), -1, &r, DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX | DT_CENTER);
    }
    if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
        r = item.rcItem; InflateRect(&r, -px(3), -px(3)); DrawFocusRect(item.hDC, &r);
    }
    SelectObject(item.hDC, oldFont);
}
void createControls() {
    auto make = [&](int id, const wchar_t* cls, const wchar_t* text, DWORD style) {
        return CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 1, 1,
            g.hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    };
    auto button = [&](int id, const wchar_t* text) {
        auto hwnd = make(id, L"BUTTON", text, WS_TABSTOP | BS_OWNERDRAW);
        SetWindowSubclass(hwnd, buttonSubclass, 1, 0);
    };
    button(Close, L"Close");
    button(Source, L"Source language");
    make(Input, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL);
    SendMessageW(control(Input), EM_SETLIMITTEXT, 1000000, 0);
    SendMessageW(control(Input), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SetWindowSubclass(control(Input), inputSubclass, 1, 0);
    button(Target, L"Target language");
    make(Output, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL);
    SendMessageW(control(Output), EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, 0);
    SetWindowSubclass(control(Output), inputSubclass, 1, 0);
    make(Status, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS | SS_NOPREFIX);
    make(Hint, L"STATIC", L"Ctrl+Enter to translate\r\nEsc to close", SS_LEFT | SS_NOPREFIX);
    button(Swap, L"Exchange languages"); button(Copy, L"Copy"); button(Run, L"Translate");
    EnableWindow(control(Copy), FALSE);
    g.tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, g.hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
    TOOLINFOW info{sizeof(info)}; info.uFlags = TTF_IDISHWND | TTF_SUBCLASS; info.hwnd = g.hwnd;
    info.uId = (UINT_PTR)control(Swap); info.lpszText = const_cast<wchar_t*>(L"Swap languages; use the completed translation as the new input");
    SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
    info.uId = (UINT_PTR)control(Copy); info.lpszText = const_cast<wchar_t*>(L"Copy translation");
    SendMessageW(g.tooltip, TTM_ADDTOOLW, 0, (LPARAM)&info);
    SendMessageW(g.tooltip, TTM_SETMAXTIPWIDTH, 0, px(360));
    configureControls(); font(); theme(); layout(); stale();
}
LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: g.hwnd = hwnd; createControls(); return 0;
    case WM_SIZE: if (wp != SIZE_MINIMIZED) layout(); return 0;
    case WM_NCHITTEST: {
        POINT point{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(hwnd, &point);
        if (point.y < px(54) && ChildWindowFromPointEx(hwnd, point, CWP_SKIPINVISIBLE) == hwnd) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_DPICHANGED: {
        g.dpi = HIWORD(wp); font(); auto* r = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right-r->left, r->bottom-r->top, SWP_NOZORDER); layout(); return 0;
    }
    case WM_SETTINGCHANGE: theme(); return 0;
    case WM_ACTIVATE: if (LOWORD(wp) != WA_INACTIVE && control(Input)) syncSettings(); return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case Run: translate(); return 0;
        case Swap: swapLanguages(); return 0;
        case Close: SendMessageW(hwnd, WM_CLOSE, 0, 0); return 0;
        case Input: if (HIWORD(wp) == EN_CHANGE) stale(); return 0;
        case Source: case Target: chooseOption(LOWORD(wp)); return 0;
        case Copy:
            if (!g.result.empty() && !g.dirty) {
                const bool ok = clipboard::SetText(g.result, false);
                status(ok ? L"Translation copied." : L"Clipboard is busy. Try again.");
                if (ok) { SetWindowTextW(control(Copy), L"Copied"); SetTimer(hwnd, 1, 1600, nullptr); }
            } return 0;
        } break;
    case WM_TIMER: SetWindowTextW(control(Copy), L"Copy"); InvalidateRect(control(Copy), nullptr, FALSE); KillTimer(hwnd, 1); return 0;
    case WM_CLOSE:
        if (g.busy) { stop(); status(L"Stopped. Your input is preserved."); }
        ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_CTLCOLOREDIT: case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: case WM_CTLCOLORLISTBOX: {
        const bool editor = (HWND)lp == control(Input) || (HWND)lp == control(Output);
        const auto p = palette(); SetTextColor((HDC)wp, editor ? p.text : p.secondary); SetBkColor((HDC)wp, p.surface);
        return (LRESULT)g.surface.get(p.surface);
    }
    case WM_DRAWITEM: drawButton(*(DRAWITEMSTRUCT*)lp); return TRUE;
    case WM_PAINT: { PAINTSTRUCT ps{}; HDC dc = BeginPaint(hwnd, &ps); paint(dc); EndPaint(hwnd, &ps); return 0; }
    case WM_ERASEBKGND: {
        return 1;
    }
    case WM_DESTROY:
        stop(); for (auto f : {g.font, g.smallFont, g.headerFont}) if (f) DeleteObject(f);
        g.font = g.smallFont = g.headerFont = nullptr; g.hwnd = nullptr; return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

namespace InputWindow {
void open(HWND appWindow) {
    g.app = appWindow;
    if (!g.hwnd) {
        g.config = g.defaults = Settings::shared().feature(Feature::Input);
        g.dpi = GetDpiForSystem();
        WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON)); RegisterClassW(&wc);
        // Keep this editable popup reachable with Alt+Tab while retaining the selection popup's chrome.
        CreateWindowExW(WS_EX_TOPMOST, kClass, L"Input translation — TypeTide-Win", WS_POPUP | WS_CLIPCHILDREN,
            0, 0, px(ui::control::PopupIdealWidth), px(400), nullptr, nullptr, wc.hInstance, nullptr);
    }
    if (!g.hwnd) return;
    if (!IsWindowVisible(g.hwnd)) {
        POINT cursor{}; GetCursorPos(&cursor);
        MONITORINFO monitor{sizeof(monitor)};
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
        const auto& work = monitor.rcWork;
        const int width = px(ui::control::PopupIdealWidth), height = px(400);
        const int x = work.left + ((work.right - work.left) - width) / 2;
        const int y = work.top + ((work.bottom - work.top) - height) / 2;
        SetWindowPos(g.hwnd, HWND_TOPMOST, x, y, width, height, SWP_NOACTIVATE);
    }
    syncSettings(); theme();
    ShowWindow(g.hwnd, SW_SHOWNORMAL); SetForegroundWindow(g.hwnd); SetFocus(control(Input));
}
bool processMessage(MSG* message) {
    if (!g.hwnd || !IsWindowVisible(g.hwnd) ||
        (message->hwnd != g.hwnd && !IsChild(g.hwnd, message->hwnd))) return false;
    if (message->message == WM_KEYDOWN && !g.composing) {
        if (message->wParam == VK_ESCAPE) { SendMessageW(g.hwnd, WM_CLOSE, 0, 0); return true; }
        if (message->wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) { translate(); return true; }
    }
    return IsDialogMessageW(g.hwnd, message) != FALSE;
}
void receive(UINT message, WPARAM request, LPARAM payload) {
    if (message == WM_APP_INPUT_DELTA) {
        auto* text = (std::wstring*)payload;
        if (g.hwnd && request == g.request && g.busy) { g.result += *text; SetWindowTextW(control(Output), g.result.c_str()); }
        delete text;
    } else {
        auto* done = (Done*)payload;
        if (g.hwnd && request == g.request && g.busy) {
            stop();
            status(done->ok ? L"Translation complete." : L"Translation failed. See details above.");
            if (!done->ok) {
                SetWindowTextW(control(Output), (L"Could not translate\r\n\r\n" + done->error).c_str());
                SetWindowTextW(control(Run), L"Retry");
            }
            EnableWindow(control(Copy), done->ok && !g.result.empty());
        }
        delete done;
    }
}
void refreshTheme() { if (g.hwnd) theme(); }
}
