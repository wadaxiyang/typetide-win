#include "SettingsWindow.h"
#include "FeatureControls.h"
#include "InputWindow.h"
#include "App.h"
#include "LaunchAtLogin.h"
#include "LocalDiagnostics.h"
#include "resource.h"
#include "Settings.h"
#include "Translator.h"
#include "ProviderProtocol.h"
#include "Credentials.h"
#include "UpdateChecker.h"
#include "Util.h"
#include "UITheme.h"
#include "VectorIcons.h"
#include "PopupWindow.h"
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <uxtheme.h>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

namespace {

constexpr wchar_t kClassName[] = L"TypeTide-WinSettings";
// 工作线程取回 Ollama 模型列表：lParam = std::vector<std::wstring>*（接收方释放）
constexpr UINT kMsgUpdateResult = WM_APP + 101;
constexpr UINT kMsgBackendHealth = WM_APP + 102;

enum CtrlId : int {
    kTab = 1000,
    // General
    kEnable, kLogin, kVersionLabel, kCheckUpdate, kFirstRunHint,
    // Providers
    kProviderList, kProviderAdd, kProviderEdit, kProviderRemove, kProviderUp, kProviderDown,
    kProviderName, kProviderKind, kProviderURL, kProviderModel, kProviderKey,
    kProviderSave, kProviderCancel, kProviderHint, kProviderEditorTitle, kProviderKeyHint,
    kBackendTest, kBackendStatus,
    // Shared language defaults
    kNativeLabel, kNative, kForeignLabel, kForeign, kLangWarn,
    // Skip apps
    kSkipEdit, kSkipAdd, kSkipList, kSkipRemove, kSkipHint,
    // Diagnostics
    kDiagPrivacy, kDiagExport, kDiagClear, kDiagPath,
    // About
    kAboutTitle, kAboutDesc, kAboutLinks,
    kAppearanceLabel, kAppearance,
    kFeature0, kFeature1, kFeature2, kFeatureNotice,
    kFeatureBack, kFeatureHotkey, kFeatureWin, kFeatureClear, kFeatureStatus,
    kFeatureSource, kFeatureTarget, kFeatureLanguageHint, kFeatureProvider, kFeatureRoute,
    kFeatureStyle, kFeatureTrigger, kFeaturePreview, kFeatureBehaviorLabel,
    kFeaturePrompt, kFeaturePromptReset, kFeatureSave, kFeatureCancel,
    // Page headings (must remain contiguous)
    kPageTitle0, kPageTitle1, kPageTitle2, kPageTitle3,
    kPageTitle4, kPageTitle5, kPageTitle6, kPageTitle7,
};

struct State {
    HWND hwnd = nullptr;
    HWND appWindow = nullptr;
    HFONT font = nullptr, fontBold = nullptr, fontTitle = nullptr;
    UINT dpi = 96;
    std::vector<std::vector<HWND>> pages;  // 每个 tab 页的控件
    bool loading = false;                  // 初始化填充时不触发保存
    bool saveWarningShown = false;
    bool backendReady = false;
    bool shortcutsReady = false;
    bool dark = false;
    bool highContrast = false;
    bool providerEditing = false;
    bool providerTesting = false;
    int providerEditIndex = -1;
    ProviderConfig providerDraft;
    std::vector<HWND> providerListControls, providerEditorControls;
    int currentPage = 0;
    Feature selectedFeature = Feature::Selection;
    FeatureConfig featureDraft;
    Hotkey featureHotkey;
    bool featureDirty = false;
    int clientWidth = 0;
    int clientHeight = 0;
    int scrollY = 0;
};

State g;
WNDPROC gHotkeyWndProc = nullptr;

HWND ctrl(int id);

int px(int v) { return MulDiv(v, (int)g.dpi, 96); }

COLORREF pageBg() { return ui::palette(g.dark, g.highContrast).background; }
COLORREF navBg() { return g.highContrast ? GetSysColor(COLOR_WINDOW) : g.dark ? RGB(0x13, 0x19, 0x24) : RGB(0xEE, 0xF1, 0xF8); }
COLORREF textColor() { return ui::palette(g.dark, g.highContrast).text; }
COLORREF secondaryColor() { return ui::palette(g.dark, g.highContrast).secondary; }
HBRUSH pageBrush() { static ui::SolidBrush brush; return brush.get(pageBg()); }
HBRUSH navBrush() { static ui::SolidBrush brush; return brush.get(navBg()); }
HBRUSH editBrush() { static ui::SolidBrush brush; return brush.get(ui::palette(g.dark, g.highContrast).surface); }

LRESULT CALLBACK hotkeyWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g.dark && msg == WM_PAINT) {
        PAINTSTRUCT ps{};
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect(dc, &rc, editBrush());
        HBRUSH border = CreateSolidBrush(GetFocus() == hwnd ? ui::Accent : ui::palette(g.dark, g.highContrast).buttonBorder);
        FrameRect(dc, &rc, border);
        DeleteObject(border);
        const WORD encoded = (WORD)SendMessageW(hwnd, HKM_GETHOTKEY, 0, 0);
        Hotkey hotkey{};
        hotkey.vk = LOBYTE(encoded);
        hotkey.modifiers = 0;
        const BYTE flags = HIBYTE(encoded);
        if (flags & HOTKEYF_CONTROL) hotkey.modifiers |= MOD_CONTROL;
        if (flags & HOTKEYF_SHIFT) hotkey.modifiers |= MOD_SHIFT;
        if (flags & HOTKEYF_ALT) hotkey.modifiers |= MOD_ALT;
        const std::wstring value = hotkey.vk ? hotkey.displayString() : L"None";
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, textColor());
        HFONT oldFont = (HFONT)SelectObject(dc, g.font);
        RECT textRect = rc;
        textRect.left += px(5);
        DrawTextW(dc, value.c_str(), -1, &textRect,
                  DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX | DT_END_ELLIPSIS);
        SelectObject(dc, oldFont);
        EndPaint(hwnd, &ps);
        return 0;
    }
    if (g.dark && msg == WM_ERASEBKGND) return 1;
    return CallWindowProcW(gHotkeyWndProc, hwnd, msg, wp, lp);
}

void enableCommonControlDarkMode() {
    // Common controls do not opt into their dark renderers merely from the
    // DWM title-bar attribute.  This process-level opt-in must happen before
    // the controls are created (supported on Windows 10 1809 and newer).
    using SetPreferredAppModeFn = int (WINAPI*)(int);
    if (HMODULE ux = GetModuleHandleW(L"uxtheme.dll")) {
        auto setMode = reinterpret_cast<SetPreferredAppModeFn>(
            GetProcAddress(ux, MAKEINTRESOURCEA(135)));
        if (setMode) setMode(1); // PreferredAppMode::AllowDark
    }
}

void applyTheme(HWND hwnd) {
    if (!hwnd) return;
    BOOL dark = g.dark && !g.highContrast;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetWindowTheme(hwnd, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    EnumChildWindows(hwnd, [](HWND child, LPARAM darkParam) -> BOOL {
        const bool darkMode = darkParam != 0;
        wchar_t cls[32]{};
        GetClassNameW(child, cls, (int)std::size(cls));
        const bool isHotkey = _wcsicmp(cls, HOTKEY_CLASSW) == 0;
        const bool needsCfd = _wcsicmp(cls, L"ComboBox") == 0 ||
                              _wcsicmp(cls, L"Edit") == 0;
        // The legacy hotkey control has no dark themed renderer.  Disable its
        // theme so WM_CTLCOLOREDIT can supply our dark background and text.
        if (darkMode && isHotkey)
            SetWindowTheme(child, L"", L"");
        else
            SetWindowTheme(child, darkMode ? (needsCfd ? L"DarkMode_CFD" : L"DarkMode_Explorer")
                                           : L"Explorer", nullptr);
        return TRUE;
    }, (LPARAM)dark);
    RedrawWindow(hwnd, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

void updateFirstRunCompletion() {
    Settings& s = Settings::shared();
    if (!s.hasCompletedFirstLaunch && g.backendReady && g.shortcutsReady) {
        s.hasCompletedFirstLaunch = true;
        s.save();
    }
    if (ctrl(kFirstRunHint)) {
        SetWindowTextW(ctrl(kFirstRunHint), s.hasCompletedFirstLaunch
            ? L"Setup checks complete. TypeTide-Win is ready."
            : L"First run: add a provider and verify the shortcuts before relying on TypeTide-Win.");
    }
}

HWND ctrl(int id) { return GetDlgItem(g.hwnd, id); }

void applyToApp() {
    if (g.appWindow) {
        // 直接调用（同线程）重装配热键 / 划词触发
        App::shared().applyEnabledState();
    }
}

// ---------- 控件创建辅助 ----------

HWND make(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h,
          int id, DWORD exStyle = 0) {
    HWND c = CreateWindowExW(exStyle, cls, text, WS_CHILD | style, px(x), px(y), px(w), px(h),
                             g.hwnd, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    if (_wcsicmp(cls, HOTKEY_CLASSW) == 0) {
        WNDPROC original = reinterpret_cast<WNDPROC>(
            SetWindowLongPtrW(c, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(hotkeyWndProc)));
        if (!gHotkeyWndProc) gHotkeyWndProc = original;
    }
    SendMessageW(c, WM_SETFONT, (WPARAM)g.font, TRUE);
    return c;
}

HWND makeLabel(const wchar_t* text, int x, int y, int w, int id, bool bold = false) {
    HWND c = make(L"STATIC", text, SS_LEFT | SS_NOPREFIX, x, y, w, 18, id);
    if (bold) SendMessageW(c, WM_SETFONT, (WPARAM)g.fontBold, TRUE);
    return c;
}

void fillLanguageCombo(HWND combo, Language selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    for (Language l : lang::All) {
        int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lang::DisplayName(l));
        if (l == selected) SendMessageW(combo, CB_SETCURSEL, idx, 0);
    }
}

Language comboLanguage(HWND combo) {
    int idx = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (idx < 0 || idx >= (int)std::size(lang::All)) return Language::English;
    return lang::All[idx];
}

void fillStyleCombo(HWND combo, RewriteStyle selected) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const RewriteStyle all[] = {RewriteStyle::Faithful, RewriteStyle::Formal,
                                RewriteStyle::Casual, RewriteStyle::Polished};
    for (RewriteStyle st : all) {
        int idx = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)lang::StyleDisplayName(st));
        if (st == selected) SendMessageW(combo, CB_SETCURSEL, idx, 0);
    }
}

RewriteStyle comboStyle(HWND combo) {
    const RewriteStyle all[] = {RewriteStyle::Faithful, RewriteStyle::Formal,
                                RewriteStyle::Casual, RewriteStyle::Polished};
    int idx = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return (idx >= 0 && idx < 4) ? all[idx] : RewriteStyle::Faithful;
}

std::wstring editText(HWND control) { return featureui::text(control); }

// HOTKEY 控件 ⇄ RegisterHotKey 修饰键转换
WORD hotkeyToControl(const Hotkey& hk) {
    BYTE flags = 0;
    if (hk.modifiers & MOD_SHIFT) flags |= HOTKEYF_SHIFT;
    if (hk.modifiers & MOD_CONTROL) flags |= HOTKEYF_CONTROL;
    if (hk.modifiers & MOD_ALT) flags |= HOTKEYF_ALT;
    return MAKEWORD(hk.vk, flags);
}

Hotkey hotkeyFromControl(WORD w) {
    Hotkey hk;
    hk.vk = LOBYTE(w);
    BYTE flags = HIBYTE(w);
    hk.modifiers = 0;
    if (flags & HOTKEYF_SHIFT) hk.modifiers |= MOD_SHIFT;
    if (flags & HOTKEYF_CONTROL) hk.modifiers |= MOD_CONTROL;
    if (flags & HOTKEYF_ALT) hk.modifiers |= MOD_ALT;
    return hk;
}

// ---------- 页构建 ----------

void showPage(int index);
int selectedProvider() { return (int)SendMessageW(ctrl(kProviderList), LB_GETCURSEL, 0, 0); }
void refreshProviders(int selected = 0) {
    const auto& providers = Settings::shared().providers;
    SendMessageW(ctrl(kProviderList), LB_RESETCONTENT, 0, 0);
    for (size_t i = 0; i < providers.size(); ++i) {
        const auto label = std::to_wstring(i + 1) + L". " + util::Widen(providers[i].name);
        SendMessageW(ctrl(kProviderList), LB_ADDSTRING, 0, (LPARAM)label.c_str());
    }
    if (!providers.empty()) SendMessageW(ctrl(kProviderList), LB_SETCURSEL, std::clamp(selected, 0, (int)providers.size() - 1), 0);
    const int index = selectedProvider();
    EnableWindow(ctrl(kProviderEdit), index >= 0);
    EnableWindow(ctrl(kProviderRemove), index >= 0);
    EnableWindow(ctrl(kProviderUp), index > 0);
    EnableWindow(ctrl(kProviderDown), index >= 0 && index + 1 < (int)providers.size());
    EnableWindow(ctrl(kBackendTest), index >= 0 && !g.providerTesting);
    EnableWindow(ctrl(kProviderAdd), providers.size() < 32);
    SetWindowTextW(ctrl(kProviderHint), providers.empty() ? L"No providers yet. Add one to start translating."
        : L"Try #1 first, then the next provider if it fails.\nFallback may send the same text to each configured service.");
    InvalidateRect(ctrl(kProviderList), nullptr, TRUE);
}
bool commitProviders(std::vector<ProviderConfig> updated, int selected) {
    auto& settings = Settings::shared();
    const auto previous = settings.providers;
    settings.providers = std::move(updated);
    if (!settings.save()) {
        settings.providers = previous;
        MessageBoxW(g.hwnd, L"Could not save providers. Check access to settings and Credential Manager.",
                    L"TypeTide-Win", MB_OK | MB_ICONERROR);
        return false;
    }
    refreshProviders(selected);
    return true;
}
void fillProviderEditor() {
    const auto& p = g.providerDraft;
    SetWindowTextW(ctrl(kProviderName), util::Widen(p.name).c_str());
    SendMessageW(ctrl(kProviderKind), CB_SETCURSEL, (int)p.kind, 0);
    SetWindowTextW(ctrl(kProviderURL), util::Widen(p.baseURL).c_str());
    SetWindowTextW(ctrl(kProviderModel), util::Widen(p.model).c_str());
    SetWindowTextW(ctrl(kProviderKey), util::Widen(p.apiKey).c_str());
    SetWindowTextW(ctrl(kProviderKeyHint), p.kind == TranslationBackend::Ollama
        ? L"Optional for local Ollama. Use a model installed on this server."
        : L"Keys are saved in Windows Credential Manager, outside settings.json.");
}
void editProvider(int index) {
    g.providerEditing = true;
    g.providerEditIndex = index;
    g.providerDraft = index >= 0 ? Settings::shared().providers[index] : NewProvider(TranslationBackend::OpenAI);
    SetWindowTextW(ctrl(kProviderEditorTitle), index >= 0 ? L"Edit provider" : L"Add provider");
    fillProviderEditor();
    showPage(1);
    SetFocus(ctrl(kProviderName));
}

void updateLangWarn() {
    const Settings& s = Settings::shared();
    SetWindowTextW(ctrl(kLangWarn),
                   s.nativeLanguage == s.foreignLanguage
                       ? L"⚠ Native and foreign are the same language — translation won't do anything useful."
                       : L"TypeTide-Win translates between your native and foreign language. Auto direction detects the text and translates the other way.");
}

void buildPages() {
    Settings::shared().ensureProviders();
    const Settings& s = Settings::shared();
    g.providerListControls.clear(); g.providerEditorControls.clear();
    g.providerEditing = false;
    g.providerTesting = false;
    g.pages.assign(8, {});
    g.featureDirty = false;
    const int x = 222, w = 560;
    int y;
    const wchar_t* pageTitles[] = {
        L"General", L"Providers", L"Features", L"Feature settings",
        L"Behavior", L"Excluded apps", L"Diagnostics", L"About & diagnostics"
    };
    for (int page = 0; page < 8; ++page) {
        HWND title = makeLabel(pageTitles[page], x, 26, w, kPageTitle0 + page);
        SendMessageW(title, WM_SETFONT, (WPARAM)g.fontTitle, TRUE);
        SetWindowPos(title, nullptr, px(x), px(26), px(w), px(36), SWP_NOZORDER);
        g.pages[page].push_back(title);
    }

    // --- 0 General ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"APP STATUS", x, y, w, -1, true),
        make(L"BUTTON", L"Enable TypeTide-Win", WS_TABSTOP | BS_AUTOCHECKBOX, x, y + 26, 180, 22, kEnable),
        makeLabel(L"STARTUP", x, y + 62, w, -1, true),
        make(L"BUTTON", L"Launch at login", WS_TABSTOP | BS_AUTOCHECKBOX, x, y + 88, 180, 22, kLogin),
        makeLabel(L"UPDATES", x, y + 150, w, -1, true),
        makeLabel(L"Version " TYPETIDE_VERSION_STRING, x, y + 178, 200, kVersionLabel),
        make(L"BUTTON", L"Check for updates", WS_TABSTOP | BS_PUSHBUTTON, x, y + 206, 150, 32, kCheckUpdate),
        makeLabel(L"", x, y + 258, w, kFirstRunHint),
        makeLabel(L"APPEARANCE", x, y + 302, w, kAppearanceLabel, true),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x, y + 332, 220, 150, kAppearance),
    };
    g.pages[0].insert(g.pages[0].end(), controls.begin(), controls.end());
    }
    CheckDlgButton(g.hwnd, kEnable, s.enabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g.hwnd, kLogin, launchatlogin::IsEnabled() ? BST_CHECKED : BST_UNCHECKED);
    for (const wchar_t* item : {L"Follow Windows", L"Light", L"Dark"})
        SendMessageW(ctrl(kAppearance), CB_ADDSTRING, 0, (LPARAM)item);
    SendMessageW(ctrl(kAppearance), CB_SETCURSEL, (WPARAM)s.appearance, 0);
    g.shortcutsReady = App::shared().shortcutsRegistered();
    updateFirstRunCompletion();

    // Providers: an ordered list and an explicit Save/Cancel editor.
    g.providerListControls = {
        makeLabel(L"", x, 82, w, kProviderHint),
        make(L"LISTBOX", L"Providers in priority order", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY |
             LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT, x, 136, w, 232, kProviderList),
        make(L"BUTTON", L"+ Add provider", WS_TABSTOP | BS_PUSHBUTTON, x, 382, 120, 32, kProviderAdd),
        make(L"BUTTON", L"Edit", WS_TABSTOP | BS_PUSHBUTTON, x + 130, 382, 64, 32, kProviderEdit),
        make(L"BUTTON", L"Remove", WS_TABSTOP | BS_PUSHBUTTON, x + 204, 382, 76, 32, kProviderRemove),
        make(L"BUTTON", L"Move up", WS_TABSTOP | BS_PUSHBUTTON, x + 358, 382, 96, 32, kProviderUp),
        make(L"BUTTON", L"Move down", WS_TABSTOP | BS_PUSHBUTTON, x + 464, 382, 96, 32, kProviderDown),
        make(L"BUTTON", L"Test selected", WS_TABSTOP | BS_PUSHBUTTON, x, 432, 120, 32, kBackendTest),
        makeLabel(L"Uses a fixed test phrase; tests only the selected provider.", x + 134, 440, w - 134, -1),
        makeLabel(L"", x, 478, w, kBackendStatus),
    };
    SetWindowPos(ctrl(kProviderHint), nullptr, px(x), px(82), px(w), px(42), SWP_NOZORDER);
    SetWindowPos(ctrl(kBackendStatus), nullptr, px(x), px(478), px(w), px(62), SWP_NOZORDER);
    SendMessageW(ctrl(kProviderList), LB_SETITEMHEIGHT, 0, px(58));
    g.providerEditorControls = {
        makeLabel(L"Add provider", x, 82, w, kProviderEditorTitle, true),
        makeLabel(L"Name", x, 124, 100, -1),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x + 112, 118, w - 112, 30, kProviderName),
        makeLabel(L"API format", x, 172, 100, -1),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 112, 166, w - 112, 200, kProviderKind),
        makeLabel(L"Base URL", x, 220, 100, -1),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x + 112, 214, w - 112, 30, kProviderURL),
        makeLabel(L"Model", x, 268, 100, -1),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x + 112, 262, w - 112, 30, kProviderModel),
        makeLabel(L"API key", x, 316, 100, -1),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD, x + 112, 310, w - 112, 30, kProviderKey),
        makeLabel(L"", x, 362, w, kProviderKeyHint),
        makeLabel(L"Use the API base URL; the app adds the request path for the selected format.", x, 396, w, -1),
        make(L"BUTTON", L"Save provider", WS_TABSTOP | BS_DEFPUSHBUTTON, x, 444, 126, 34, kProviderSave),
        make(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, x + 138, 444, 90, 34, kProviderCancel),
    };
    for (const auto* kind : {L"Ollama", L"OpenAI-compatible", L"Anthropic (Messages)", L"Gemini (Generate Content)"})
        SendMessageW(ctrl(kProviderKind), CB_ADDSTRING, 0, (LPARAM)kind);
    for (int id : {kProviderName, kProviderModel, kProviderURL, kProviderKey})
        SendMessageW(ctrl(id), EM_SETLIMITTEXT, id == kProviderName ? 100 : id == kProviderModel ? 256 : 2000, 0);
    g.pages[1].insert(g.pages[1].end(), g.providerListControls.begin(), g.providerListControls.end());
    g.pages[1].insert(g.pages[1].end(), g.providerEditorControls.begin(), g.providerEditorControls.end());
    refreshProviders();

    // Shared language preferences are set once; features may override them.
    g.pages[0].push_back(makeLabel(L"LANGUAGE DEFAULTS", x, 474, w, -1, true));
    g.pages[0].push_back(makeLabel(L"Native language", x, 510, 170, kNativeLabel));
    g.pages[0].push_back(make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 190, 506, 300, 260, kNative));
    g.pages[0].push_back(makeLabel(L"Common foreign language", x, 552, 180, kForeignLabel));
    g.pages[0].push_back(make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 190, 548, 300, 260, kForeign));
    g.pages[0].push_back(make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, 596, w, 48, kLangWarn));
    fillLanguageCombo(ctrl(kNative), s.nativeLanguage);
    fillLanguageCombo(ctrl(kForeign), s.foreignLanguage);
    updateLangWarn();

    // Features overview: exactly three stable actions, each with its own settings.
    for (int i = 0; i < 3; ++i)
        g.pages[2].push_back(make(L"BUTTON", FeatureName((Feature)i), WS_TABSTOP | BS_OWNERDRAW,
            x, 94 + i * 128, w, 108, kFeature0 + i));
    g.pages[2].push_back(make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, 496, w, 116, kFeatureNotice));

    g.pages[3].insert(g.pages[3].end(), {
        make(L"BUTTON", L"← Features", WS_TABSTOP | BS_PUSHBUTTON, x, 76, 112, 28, kFeatureBack),
        makeLabel(L"Shortcut", x, 124, 120, -1),
        make(HOTKEY_CLASSW, L"", WS_TABSTOP, x + 142, 118, 210, 28, kFeatureHotkey),
        make(L"BUTTON", L"Win", WS_TABSTOP | BS_AUTOCHECKBOX, x + 364, 120, 64, 26, kFeatureWin),
        make(L"BUTTON", L"Clear", WS_TABSTOP | BS_PUSHBUTTON, x + 446, 118, 80, 28, kFeatureClear),
        makeLabel(L"", x + 142, 152, w - 142, kFeatureStatus),
        makeLabel(L"Source language", x, 186, 250, -1),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x, 210, 266, 280, kFeatureSource),
        makeLabel(L"Target rule", x + 290, 186, 250, -1),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x + 290, 210, 270, 280, kFeatureTarget),
        make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, 246, w, 40, kFeatureLanguageHint),
        makeLabel(L"Provider", x, 300, 120, -1),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, x + 142, 294, w - 142, 280, kFeatureProvider),
        make(L"STATIC", L"", SS_LEFT | SS_NOPREFIX, x, 332, w, 46, kFeatureRoute),
        makeLabel(L"Output style", x, 388, 120, -1),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 142, 382, 270, 180, kFeatureStyle),
        makeLabel(L"Selection trigger", x, 430, 130, kFeatureBehaviorLabel),
        make(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST, x + 142, 424, w - 142, 200, kFeatureTrigger),
        make(L"BUTTON", L"Preview before replacing", WS_TABSTOP | BS_AUTOCHECKBOX, x, 424, w, 28, kFeaturePreview),
        makeLabel(L"Provider prompt", x, 470, w, -1, true),
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL,
             x, 500, w, 100, kFeaturePrompt),
        makeLabel(L"{source}, {target}, {style} follow your options. Input text is added automatically.", x, 610, w, -1),
        make(L"BUTTON", L"Restore default prompt", WS_TABSTOP | BS_PUSHBUTTON, x, 650, 188, 32, kFeaturePromptReset),
        make(L"BUTTON", L"Cancel", WS_TABSTOP | BS_PUSHBUTTON, x + 360, 650, 90, 32, kFeatureCancel),
        make(L"BUTTON", L"Save", WS_TABSTOP | BS_PUSHBUTTON, x + 466, 650, 94, 32, kFeatureSave),
    });
    SendMessageW(ctrl(kFeaturePrompt), EM_SETLIMITTEXT, 8000, 0);
    for (auto label : {L"Shortcut only", L"Show floating icon", L"Auto-translate selection"})
        SendMessageW(ctrl(kFeatureTrigger), CB_ADDSTRING, 0, (LPARAM)label);

    // --- 5 Skip Apps ---
    y = 82;
    {
    std::vector<HWND> controls = {
        make(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, x, y, 280, 24, kSkipEdit),
        make(L"BUTTON", L"Add", WS_TABSTOP | BS_PUSHBUTTON, x + 290, y - 1, 70, 26, kSkipAdd),
        make(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | WS_VSCROLL | LBS_NOTIFY, x, y + 34, 360, 240, kSkipList),
        make(L"BUTTON", L"Remove", WS_TABSTOP | BS_PUSHBUTTON, x + 370, y + 34, 80, 26, kSkipRemove),
        makeLabel(L"TypeTide-Win stays inactive in these apps (executable name, e.g. notepad.exe or code.exe).",
                  x, y + 284, w, kSkipHint),
    };
    g.pages[5].insert(g.pages[5].end(), controls.begin(), controls.end());
    }
    for (auto& app : s.skipApps)
        SendMessageW(ctrl(kSkipList), LB_ADDSTRING, 0, (LPARAM)app.c_str());

    // --- 6 Diagnostics ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"Stored only on this PC. Never includes selected text, translations, clipboard contents, app names, URLs, or credentials.",
                  x, y, w, kDiagPrivacy),
        makeLabel(L"Report file:", x, y + 52, 90, -1),
        makeLabel(diagnostics::Path().c_str(), x + 90, y + 52, w - 90, kDiagPath),
        make(L"BUTTON", L"Export report…", WS_TABSTOP | BS_PUSHBUTTON, x, y + 92, 130, 30, kDiagExport),
        make(L"BUTTON", L"Clear diagnostics", WS_TABSTOP | BS_PUSHBUTTON, x + 142, y + 92, 130, 30, kDiagClear),
    };
    g.pages[6].insert(g.pages[6].end(), controls.begin(), controls.end());
    }
    SetWindowPos(ctrl(kDiagPrivacy), nullptr, px(x), px(y), px(w), px(40), SWP_NOZORDER);

    // --- 7 About ---
    y = 82;
    {
    std::vector<HWND> controls = {
        makeLabel(L"TypeTide-Win " TYPETIDE_VERSION_STRING, x, y, 300, kAboutTitle, true),
        makeLabel(L"System-wide AI translation && inline rewrite for Windows.\n"
                  L"Ollama, OpenAI-compatible, Anthropic and Gemini providers.",
                  x, y + 28, w, kAboutDesc),
        make(L"SysLink",
             L"<a href=\"https://github.com/wadaxiyang/typetide-win\">Project</a>   ·   "
             L"<a href=\"https://github.com/everettjf/typetide\">Upstream: everettjf/TypeTide</a>",
             WS_TABSTOP, x, y + 84, w, 22, kAboutLinks),
    };
    g.pages[7].insert(g.pages[7].end(), controls.begin(), controls.end());
    }
    DestroyWindow(ctrl(kPageTitle4)); g.pages[4].clear();
    for (HWND c : g.pages[6]) {
        if (c == ctrl(kPageTitle6)) { DestroyWindow(c); continue; }
        RECT r{}; GetWindowRect(c, &r); MapWindowPoints(nullptr, g.hwnd, (POINT*)&r, 2);
        SetWindowPos(c, nullptr, r.left, r.top + px(190), r.right - r.left, r.bottom - r.top, SWP_NOZORDER);
        g.pages[7].push_back(c);
    }
    g.pages[6].clear();
    g.pages[7].push_back(makeLabel(L"LOCAL DIAGNOSTICS", x, 232, w, -1, true));
    // 描述区两行高
    SetWindowPos(ctrl(kAboutDesc), nullptr, px(x), px(y + 28), px(w), px(40), SWP_NOZORDER);
}

void featureHints() {
    const auto& s = Settings::shared();
    const auto& c = g.featureDraft;
    std::wstring language = s.languageLabel(c.source, true) + L" → " + s.languageLabel(c.target);
    if (c.target == kLanguageAuto) language += g.selectedFeature == Feature::Selection
        ? L". Uncertain text goes to your native language."
        : L". Uncertain text goes to your foreign language.";
    SetWindowTextW(ctrl(kFeatureLanguageHint), language.c_str());
    SetWindowTextW(ctrl(kFeatureRoute), s.providerSummary(c).c_str());
}
void openFeatureEditor(Feature feature) {
    g.selectedFeature = feature;
    const auto& s = Settings::shared();
    g.featureDraft = s.feature(feature); g.featureHotkey = s.shortcut(feature);
    g.loading = true;
    SetWindowTextW(ctrl(kPageTitle3), FeatureName(feature));
    SendMessageW(ctrl(kFeatureHotkey), HKM_SETHOTKEY, hotkeyToControl(g.featureHotkey), 0);
    CheckDlgButton(g.hwnd, kFeatureWin, g.featureHotkey.modifiers & MOD_WIN ? BST_CHECKED : BST_UNCHECKED);
    SetWindowTextW(ctrl(kFeatureStatus), App::shared().shortcutStatus(feature).c_str());
    featureui::languages(ctrl(kFeatureSource), g.featureDraft.source, true);
    featureui::languages(ctrl(kFeatureTarget), g.featureDraft.target, false);
    featureui::providers(ctrl(kFeatureProvider), g.featureDraft);
    fillStyleCombo(ctrl(kFeatureStyle), g.featureDraft.style);
    SetWindowTextW(ctrl(kFeaturePrompt), util::Widen(g.featureDraft.prompt.empty()
        ? translator::DefaultPromptTemplate() : g.featureDraft.prompt).c_str());
    SendMessageW(ctrl(kFeatureTrigger), CB_SETCURSEL, (int)s.selectionTrigger, 0);
    CheckDlgButton(g.hwnd, kFeaturePreview, s.rewritePreview ? BST_CHECKED : BST_UNCHECKED);
    featureHints();
    g.loading = false; g.featureDirty = false;
    showPage(3);
    SetFocus(ctrl(kFeatureHotkey));
}
bool leaveFeatureEditor() {
    if (g.currentPage != 3 || !g.featureDirty) return true;
    return MessageBoxW(g.hwnd, L"Discard the unsaved changes to this feature?", L"TypeTide-Win",
                       MB_OKCANCEL | MB_ICONQUESTION) == IDOK;
}

void scrollPage(int position) {
    const int delta = g.scrollY - position;
    for (auto control : g.pages[g.currentPage]) {
        RECT r{}; GetWindowRect(control, &r); MapWindowPoints(nullptr, g.hwnd, (POINT*)&r, 2);
        SetWindowPos(control, nullptr, r.left, r.top + delta, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
    g.scrollY = position;
    SetScrollPos(g.hwnd, SB_VERT, position, TRUE);
    InvalidateRect(g.hwnd, nullptr, TRUE);
}
void updateScroll() {
    if (g.pages.empty()) return;
    const int heights[] = {670, 510, 640, 710, 0, 540, 0, 530};
    RECT r{}; GetClientRect(g.hwnd, &r);
    SCROLLINFO info{sizeof(info), SIF_RANGE | SIF_PAGE | SIF_POS};
    info.nMax = px(heights[g.currentPage]); info.nPage = r.bottom;
    const int position = std::clamp(g.scrollY, 0, std::max(0, info.nMax - (int)info.nPage + 1));
    if (position != g.scrollY) scrollPage(position);
    info.nPos = position; SetScrollInfo(g.hwnd, SB_VERT, &info, TRUE);
}

void showPage(int index) {
    if (g.scrollY) scrollPage(0);
    g.currentPage = index;
    for (int p = 0; p < (int)g.pages.size(); ++p)
        for (HWND c : g.pages[p])
            ShowWindow(c, p == index ? SW_SHOW : SW_HIDE);
    if (index == 2) SettingsWindow::refreshFeatures();
    if (index == 3) {
        ShowWindow(ctrl(kFeatureTrigger), g.selectedFeature == Feature::Selection ? SW_SHOW : SW_HIDE);
        ShowWindow(ctrl(kFeatureBehaviorLabel), g.selectedFeature == Feature::Selection ? SW_SHOW : SW_HIDE);
        ShowWindow(ctrl(kFeaturePreview), g.selectedFeature == Feature::Rewrite ? SW_SHOW : SW_HIDE);
    }
    if (index == 1) {
        for (auto c : g.providerListControls) ShowWindow(c, g.providerEditing ? SW_HIDE : SW_SHOW);
        for (auto c : g.providerEditorControls) ShowWindow(c, g.providerEditing ? SW_SHOW : SW_HIDE);
    }
    updateScroll();
}

void layoutForClientSize(int width, int height) {
    if (!g.hwnd || width <= 0 || height <= 0) return;
    if (g.clientWidth == 0 || g.clientHeight == 0) {
        g.clientWidth = width;
        g.clientHeight = height;
        return;
    }

    const int dx = width - g.clientWidth;
    const int dy = height - g.clientHeight;
    if (dx == 0 && dy == 0) return;

    HWND nav = ctrl(kTab);
    if (nav) {
        RECT r{};
        GetWindowRect(nav, &r);
        MapWindowPoints(nullptr, g.hwnd, reinterpret_cast<POINT*>(&r), 2);
        const int navWidth = static_cast<int>(r.right - r.left);
        const int navHeight = std::max(px(300), static_cast<int>(r.bottom - r.top) + dy);
        SetWindowPos(nav, nullptr, r.left, r.top, navWidth, navHeight, SWP_NOZORDER);
    }

    for (const auto& page : g.pages) {
        for (HWND control : page) {
            RECT r{};
            GetWindowRect(control, &r);
            MapWindowPoints(nullptr, g.hwnd, reinterpret_cast<POINT*>(&r), 2);
            int controlWidth = r.right - r.left;
            int controlHeight = r.bottom - r.top;
            const int rightGap = g.clientWidth - r.right;
            const int id = GetDlgCtrlID(control);
            if (rightGap <= px(48) || (id >= kPageTitle0 && id <= kPageTitle7))
                controlWidth = std::max(px(80), controlWidth + dx);
            if (id == kSkipList)
                controlHeight = std::max(px(120), controlHeight + dy);
            SetWindowPos(control, nullptr, r.left, r.top, controlWidth, controlHeight,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    g.clientWidth = width;
    g.clientHeight = height;
    InvalidateRect(g.hwnd, nullptr, TRUE);
}

// ---------- 变更处理 ----------

void onCommand(int id, int code) {
    if (g.loading) return;
    Settings& s = Settings::shared();
    bool save = true, reapply = false;

    switch (id) {
    case kAppearance:
        if (code != CBN_SELCHANGE) return;
        s.appearance = (Appearance)SendMessageW(ctrl(kAppearance), CB_GETCURSEL, 0, 0);
        g.dark = s.useDarkMode();
        applyTheme(g.hwnd);
        PopupWindow::shared().refreshTheme();
        InputWindow::refreshTheme();
        break;
    case kTab:
        if (code == LBN_SELCHANGE)
        {
            const int pages[] = {0, 1, 2, 5, 7};
            const int selected = (int)SendMessageW(ctrl(kTab), LB_GETCURSEL, 0, 0);
            if (!leaveFeatureEditor()) { SendMessageW(ctrl(kTab), LB_SETCURSEL, 2, 0); return; }
            g.featureDirty = false;
            if (selected >= 0 && selected < 5) showPage(pages[selected]);
        }
        return;
    case kEnable:
        s.enabled = IsDlgButtonChecked(g.hwnd, kEnable) == BST_CHECKED;
        reapply = true;
        break;
    case kLogin:
        launchatlogin::SetEnabled(IsDlgButtonChecked(g.hwnd, kLogin) == BST_CHECKED);
        save = false;
        break;
    case kCheckUpdate:
        EnableWindow(ctrl(kCheckUpdate), FALSE);
        SetWindowTextW(ctrl(kCheckUpdate), L"Checking…");
        updatechecker::CheckNowInteractive(g.hwnd, kMsgUpdateResult);
        save = false;
        break;

    case kProviderList:
        if (code == LBN_DBLCLK && selectedProvider() >= 0) editProvider(selectedProvider());
        else if (code == LBN_SELCHANGE) refreshProviders(selectedProvider());
        return;
    case kProviderAdd: editProvider(-1); return;
    case kProviderEdit: if (selectedProvider() >= 0) editProvider(selectedProvider()); return;
    case kProviderCancel: g.providerEditing = false; showPage(1); return;
    case kProviderKind:
        if (code == CBN_SELCHANGE) {
            const auto kind = (TranslationBackend)SendMessageW(ctrl(kProviderKind), CB_GETCURSEL, 0, 0);
            if (kind != g.providerDraft.kind) {
                auto changed = NewProvider(kind);
                changed.id = g.providerDraft.id;
                // A protocol change must not accidentally send the old key to a new host.
                g.providerDraft = std::move(changed);
                fillProviderEditor();
            }
        }
        return;
    case kProviderSave: {
        auto p = g.providerDraft;
        p.name = util::Narrow(util::Trim(editText(ctrl(kProviderName))));
        p.baseURL = util::Narrow(util::Trim(editText(ctrl(kProviderURL))));
        while (!p.baseURL.empty() && p.baseURL.back() == '/') p.baseURL.pop_back();
        p.model = util::Narrow(util::Trim(editText(ctrl(kProviderModel))));
        p.apiKey = util::Narrow(util::Trim(editText(ctrl(kProviderKey))));
        std::wstring error;
        if (!provider::Validate(p, error)) { MessageBoxW(g.hwnd, error.c_str(), L"Provider settings", MB_OK | MB_ICONWARNING); return; }
        auto updated = s.providers;
        const int index = g.providerEditIndex < 0 ? (int)updated.size() : g.providerEditIndex;
        if (g.providerEditIndex < 0) updated.push_back(p); else updated[index] = p;
        if (commitProviders(std::move(updated), index)) { g.providerEditing = false; showPage(1); }
        return;
    }
    case kProviderUp:
    case kProviderDown:
    case kProviderRemove: {
        const int index = selectedProvider();
        if (index < 0) return;
        auto updated = s.providers;
        int next = index;
        if (id == kProviderRemove) updated.erase(updated.begin() + index);
        else {
            next += id == kProviderUp ? -1 : 1;
            if (next < 0 || next >= (int)updated.size()) return;
            std::swap(updated[index], updated[next]);
        }
        const auto removedId = s.providers[index].id;
        if (commitProviders(std::move(updated), next) && id == kProviderRemove) {
            wchar_t testDir[2]{};
            if (!GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir, 2)) credentials::SaveProviderKey(removedId, "");
        }
        return;
    }
    case kBackendTest: {
        const int index = selectedProvider();
        if (index < 0) return;
        const auto config = s.providers[index];
        g.providerTesting = true;
        EnableWindow(ctrl(kBackendTest), FALSE);
        SetWindowTextW(ctrl(kBackendTest), L"Testing…");
        SetWindowTextW(ctrl(kBackendStatus), (L"Testing " + util::Widen(config.name) + L"…").c_str());
        HWND target = g.hwnd;
        translator::CheckHealthAsync([target, name = config.name](translator::HealthCheckResult result) {
            result.message = util::Widen(name) + L": " + result.message;
            auto* payload = new translator::HealthCheckResult(std::move(result));
            if (!PostMessageW(target, kMsgBackendHealth, 0, (LPARAM)payload)) delete payload;
        }, config);
        return;
    }

    case kNative: case kForeign:
        if (code != CBN_SELCHANGE) return;
        s.nativeLanguage = comboLanguage(ctrl(kNative)); s.foreignLanguage = comboLanguage(ctrl(kForeign));
        updateLangWarn(); break;
    case kFeature0: case kFeature1: case kFeature2:
        openFeatureEditor((Feature)(id - kFeature0)); return;
    case kFeatureBack: case kFeatureCancel:
        if (leaveFeatureEditor()) { g.featureDirty = false; showPage(2); SetFocus(ctrl(kFeature0 + (int)g.selectedFeature)); }
        return;
    case kFeatureClear:
        g.loading = true;
        SendMessageW(ctrl(kFeatureHotkey), HKM_SETHOTKEY, 0, 0);
        CheckDlgButton(g.hwnd, kFeatureWin, BST_UNCHECKED);
        g.loading = false;
        g.featureHotkey = {0, 0}; g.featureDirty = true; return;
    case kFeatureHotkey: case kFeatureWin:
        if (id == kFeatureHotkey && code != EN_CHANGE) return;
        g.featureHotkey = hotkeyFromControl((WORD)SendMessageW(ctrl(kFeatureHotkey), HKM_GETHOTKEY, 0, 0));
        if (g.featureHotkey.isConfigured() && IsDlgButtonChecked(g.hwnd, kFeatureWin) == BST_CHECKED)
            g.featureHotkey.modifiers |= MOD_WIN;
        SetWindowTextW(ctrl(kFeatureStatus), L"Availability will be checked when saved.");
        g.featureDirty = true; return;
    case kFeatureSource: case kFeatureTarget: case kFeatureProvider: case kFeatureStyle:
        if (code != CBN_SELCHANGE) return;
        g.featureDraft.source = featureui::value(ctrl(kFeatureSource));
        g.featureDraft.target = featureui::value(ctrl(kFeatureTarget));
        g.featureDraft.style = comboStyle(ctrl(kFeatureStyle));
        if (id == kFeatureProvider) {
            const int index = featureui::value(ctrl(kFeatureProvider));
            const auto list = s.effectiveProviders();
            if (index >= 0 && index < (int)list.size()) g.featureDraft.providerId = list[index].id;
            else if (index == -1) g.featureDraft.providerId.clear();
        }
        featureHints(); g.featureDirty = true; return;
    case kFeaturePromptReset:
        SetWindowTextW(ctrl(kFeaturePrompt), util::Widen(translator::DefaultPromptTemplate()).c_str());
        g.featureDirty = true; return;
    case kFeaturePrompt:
        if (code == EN_CHANGE) g.featureDirty = true;
        return;
    case kFeatureTrigger: case kFeaturePreview:
        g.featureDirty = true; return;
    case kFeatureSave: {
        for (int i = 0; i < 3; ++i) {
            if ((Feature)i != g.selectedFeature && g.featureHotkey.isConfigured() && g.featureHotkey == s.shortcut((Feature)i)) {
                SetWindowTextW(ctrl(kFeatureStatus), (L"Already used by " + std::wstring(FeatureName((Feature)i))).c_str());
                SetFocus(ctrl(kFeatureHotkey)); return;
            }
        }
        g.featureDraft.prompt = util::Narrow(util::Trim(featureui::text(ctrl(kFeaturePrompt))));
        if (g.featureDraft.prompt == translator::DefaultPromptTemplate()) g.featureDraft.prompt.clear();
        const Settings previous = s;
        s.feature(g.selectedFeature) = g.featureDraft;
        s.shortcut(g.selectedFeature) = g.featureHotkey;
        if (g.selectedFeature == Feature::Selection) s.selectionTrigger = (SelectionTrigger)SendMessageW(ctrl(kFeatureTrigger), CB_GETCURSEL, 0, 0);
        if (g.selectedFeature == Feature::Rewrite) s.rewritePreview = IsDlgButtonChecked(g.hwnd, kFeaturePreview) == BST_CHECKED;
        if (!s.save()) {
            s = previous;
            MessageBoxW(g.hwnd, L"Could not save the feature settings. Your previous settings were kept.", L"TypeTide-Win", MB_OK | MB_ICONERROR);
            return;
        }
        g.featureDirty = false; applyToApp(); showPage(2); SetFocus(ctrl(kFeature0 + (int)g.selectedFeature)); return;
    }
    case kSkipAdd: {
        std::wstring app = util::Trim(editText(ctrl(kSkipEdit)));
        if (app.empty()) { save = false; break; }
        for (auto& e : s.skipApps)
            if (util::ToLower(e) == util::ToLower(app)) { save = false; break; }
        if (!save) break;
        s.skipApps.push_back(app);
        SendMessageW(ctrl(kSkipList), LB_ADDSTRING, 0, (LPARAM)app.c_str());
        SetWindowTextW(ctrl(kSkipEdit), L"");
        break;
    }
    case kSkipRemove: {
        int sel = (int)SendMessageW(ctrl(kSkipList), LB_GETCURSEL, 0, 0);
        if (sel < 0 || sel >= (int)s.skipApps.size()) { save = false; break; }
        s.skipApps.erase(s.skipApps.begin() + sel);
        SendMessageW(ctrl(kSkipList), LB_DELETESTRING, sel, 0);
        break;
    }
    case kDiagExport: {
        wchar_t path[MAX_PATH] = L"TypeTide-Win-Diagnostics.json";
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = g.hwnd;
        dialog.lpstrFilter = L"JSON report\0*.json\0All files\0*.*\0";
        dialog.lpstrFile = path;
        dialog.nMaxFile = MAX_PATH;
        dialog.lpstrDefExt = L"json";
        dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        if (GetSaveFileNameW(&dialog)) {
            if (!CopyFileW(diagnostics::Path().c_str(), path, FALSE))
                MessageBoxW(g.hwnd, L"No diagnostics have been recorded yet.",
                            L"TypeTide-Win — export", MB_OK | MB_ICONINFORMATION);
        }
        save = false;
        break;
    }
    case kDiagClear:
        diagnostics::Clear();
        MessageBoxW(g.hwnd, L"Local diagnostics were cleared.", L"TypeTide-Win — diagnostics",
                    MB_OK | MB_ICONINFORMATION);
        save = false;
        break;

    default:
        return;
    }

    if (save) {
        if (s.save()) {
            g.saveWarningShown = false;
        } else if (!g.saveWarningShown) {
            g.saveWarningShown = true;
            MessageBoxW(g.hwnd,
                        L"TypeTide-Win couldn't save your settings. Your existing settings file was "
                        L"left unchanged.\n\nCheck that the settings folder is writable, then try again.",
                        L"TypeTide-Win — settings not saved", MB_OK | MB_ICONWARNING);
        }
    }
    if (reapply) applyToApp();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lp);
        RECT minimum{0, 0, px(820), px(560)};
        AdjustWindowRect(&minimum, WS_OVERLAPPEDWINDOW, FALSE);
        info->ptMinTrackSize = {minimum.right - minimum.left, minimum.bottom - minimum.top};
        return 0;
    }
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            layoutForClientSize(LOWORD(lp), HIWORD(lp));
            updateScroll();
        }
        return 0;
    case WM_VSCROLL: {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(hwnd, SB_VERT, &info);
        int position = g.scrollY;
        switch (LOWORD(wp)) {
        case SB_LINEUP: position -= px(32); break;
        case SB_LINEDOWN: position += px(32); break;
        case SB_PAGEUP: position -= info.nPage; break;
        case SB_PAGEDOWN: position += info.nPage; break;
        case SB_THUMBTRACK: position = info.nTrackPos; break;
        default: return 0;
        }
        scrollPage(std::clamp(position, 0, std::max(0, info.nMax - (int)info.nPage + 1))); return 0;
    }
    case WM_MOUSEWHEEL: {
        SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(hwnd, SB_VERT, &info);
        scrollPage(std::clamp(g.scrollY - (int)(short)HIWORD(wp) * px(48) / WHEEL_DELTA,
                             0, std::max(0, info.nMax - (int)info.nPage + 1))); return 0;
    }
    case WM_COMMAND:
        onCommand(LOWORD(wp), HIWORD(wp));
        return 0;
    case kMsgUpdateResult:
        EnableWindow(ctrl(kCheckUpdate), TRUE);
        SetWindowTextW(ctrl(kCheckUpdate), L"Check for updates");
        if (wp == 1) {
            if (MessageBoxW(hwnd, L"A new TypeTide-Win version is available. Open the download page?",
                            L"TypeTide-Win update", MB_YESNO | MB_ICONINFORMATION) == IDYES)
                updatechecker::OpenReleasesPage();
        } else if (wp == 0) {
            MessageBoxW(hwnd, L"You're using the latest version.", L"TypeTide-Win update",
                        MB_OK | MB_ICONINFORMATION);
        } else {
            MessageBoxW(hwnd,
                        L"TypeTide-Win couldn't check for updates. Check your internet connection and try again.",
                        L"TypeTide-Win update", MB_OK | MB_ICONWARNING);
        }
        return 0;
    case kMsgBackendHealth: {
        auto* result = (translator::HealthCheckResult*)lp;
        g.providerTesting = false;
        EnableWindow(ctrl(kBackendTest), selectedProvider() >= 0);
        SetWindowTextW(ctrl(kBackendTest), L"Test selected");
        std::wstring status = result->message;
        if (result->ok) {
            status += L"  ";
            if (result->firstTokenMs >= 0)
                status += L"First token " + std::to_wstring(result->firstTokenMs) + L" ms · ";
            status += L"Total " + std::to_wstring(result->totalMs) + L" ms";
        }
        SetWindowTextW(ctrl(kBackendStatus), status.c_str());
        g.backendReady = result->ok;
        updateFirstRunCompletion();
        delete result;
        return 0;
    }
    case WM_NOTIFY: {
        auto* hdr = (NMHDR*)lp;
        if (hdr->idFrom == kAboutLinks && hdr->code == NM_CUSTOMDRAW) {
            auto* draw = reinterpret_cast<NMCUSTOMDRAW*>(lp);
            if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
                SetTextColor(draw->hdc, ui::accent(g.dark, g.highContrast));
                return CDRF_NEWFONT;
            }
        }
        if (hdr->idFrom == kAboutLinks && (hdr->code == NM_CLICK || hdr->code == NM_RETURN)) {
            auto* link = (NMLINK*)lp;
            ShellExecuteW(nullptr, L"open", link->item.szUrl, nullptr, nullptr, SW_SHOWNORMAL);
            return 0;
        }
        break;
    }
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (item->CtlID >= kFeature0 && item->CtlID <= kFeature2) {
            const auto feature = (Feature)(item->CtlID - kFeature0);
            const auto& s = Settings::shared(); const auto& c = s.feature(feature);
            const auto p = ui::palette(g.dark, g.highContrast);
            const auto oldFont = SelectObject(item->hDC, g.fontBold);
            const auto brush = CreateSolidBrush(p.surface); const auto border = CreatePen(PS_SOLID, 1, p.divider);
            const auto oldBrush = SelectObject(item->hDC, brush), oldPen = SelectObject(item->hDC, border);
            const RECT r = item->rcItem;
            RoundRect(item->hDC, r.left, r.top, r.right, r.bottom, px(12), px(12));
            SetBkMode(item->hDC, TRANSPARENT); SetTextColor(item->hDC, p.text);
            RECT text{r.left + px(18), r.top + px(12), r.right - px(24), r.top + px(38)};
            DrawTextW(item->hDC, FeatureName(feature), -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(item->hDC, g.font); SetTextColor(item->hDC, p.secondary);
            const wchar_t* behavior = feature == Feature::Input ? L"Type & copy  ›" : feature == Feature::Rewrite
                ? (s.rewritePreview ? L"Preview first  ›" : L"Direct replacement  ›")
                : s.selectionTrigger == SelectionTrigger::Icon ? L"Floating icon  ›"
                : s.selectionTrigger == SelectionTrigger::Auto ? L"Auto-translate  ›" : L"Shortcut only  ›";
            RECT hint{r.right - px(190), r.top + px(14), r.right - px(18), r.top + px(36)};
            DrawTextW(item->hDC, behavior, -1, &hint, DT_SINGLELINE | DT_RIGHT | DT_NOPREFIX);
            text.top = r.top + px(46); text.bottom = text.top + px(22);
            const auto line = s.shortcut(feature).displayString() + L" · " + App::shared().shortcutStatus(feature) +
                L"   |   " + s.languageLabel(c.source, true) + L" → " + s.languageLabel(c.target);
            DrawTextW(item->hDC, line.c_str(), -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            text.top += px(26); text.bottom += px(26);
            const auto route = s.providerSummary(c);
            DrawTextW(item->hDC, route.c_str(), -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            if (item->itemState & ODS_FOCUS) { RECT focus = r; InflateRect(&focus, -px(4), -px(4)); DrawFocusRect(item->hDC, &focus); }
            SelectObject(item->hDC, oldBrush); SelectObject(item->hDC, oldPen); SelectObject(item->hDC, oldFont);
            DeleteObject(brush); DeleteObject(border); return TRUE;
        }
        if (item->CtlID == kProviderList && item->itemID != (UINT)-1) {
            const auto& providers = Settings::shared().providers;
            if (item->itemID >= providers.size()) return TRUE;
            const auto& p = providers[item->itemID];
            const bool selected = (item->itemState & ODS_SELECTED) != 0;
            const auto palette = ui::palette(g.dark, g.highContrast);
            const COLORREF bgColor = selected ? (g.highContrast ? GetSysColor(COLOR_HIGHLIGHT)
                : g.dark ? RGB(0x2C, 0x32, 0x50) : RGB(0xDF, 0xE6, 0xFA)) : palette.surface;
            HBRUSH bg = CreateSolidBrush(bgColor); FillRect(item->hDC, &item->rcItem, bg); DeleteObject(bg);
            SetBkMode(item->hDC, TRANSPARENT);
            SetTextColor(item->hDC, selected && g.highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : textColor());
            const auto oldFont = SelectObject(item->hDC, g.font);
            RECT number = item->rcItem; number.left += px(14); number.right = number.left + px(28);
            const auto rank = std::to_wstring(item->itemID + 1);
            DrawTextW(item->hDC, rank.c_str(), -1, &number, DT_SINGLELINE | DT_VCENTER);
            RECT text = item->rcItem; text.left += px(52); text.right -= px(12); text.top += px(8); text.bottom = text.top + px(20);
            const auto name = util::Widen(p.name);
            DrawTextW(item->hDC, name.c_str(), -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SetTextColor(item->hDC, selected && g.highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : secondaryColor());
            text.top += px(23); text.bottom += px(23);
            const auto detail = util::Widen(std::string(ProviderKindCode(p.kind)) + "  ·  " + p.model);
            DrawTextW(item->hDC, detail.c_str(), -1, &text, DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(item->hDC, oldFont);
            if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
                RECT focus = item->rcItem; InflateRect(&focus, -px(3), -px(3)); DrawFocusRect(item->hDC, &focus);
            }
            return TRUE;
        }
        if (item->CtlID != kTab || item->itemID == (UINT)-1) break;
        const bool selected = (item->itemState & ODS_SELECTED) != 0;
        HBRUSH bg = CreateSolidBrush(selected ? (g.highContrast ? GetSysColor(COLOR_HIGHLIGHT) : g.dark ? RGB(0x2C, 0x32, 0x50) : RGB(0xDF, 0xE6, 0xFA)) : navBg());
        FillRect(item->hDC, &item->rcItem, bg);
        DeleteObject(bg);
        if (selected) {
            RECT accent{item->rcItem.left, item->rcItem.top + px(7),
                        item->rcItem.left + px(3), item->rcItem.bottom - px(7)};
            HBRUSH ab = CreateSolidBrush(ui::accent(g.dark, g.highContrast));
            FillRect(item->hDC, &accent, ab);
            DeleteObject(ab);
        }
        wchar_t text[64]{};
        SendMessageW(item->hwndItem, LB_GETTEXT, item->itemID, (LPARAM)text);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, selected && g.highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : textColor());
        SelectObject(item->hDC, g.font);
        RECT tr = item->rcItem;
        RECT iconRect{item->rcItem.left + px(17), item->rcItem.top + px(11),
                      item->rcItem.left + px(37), item->rcItem.top + px(31)};
        vectoricons::DrawNavigation(item->hDC, iconRect, (item->itemID == 3 ? 5 : item->itemID == 4 ? 7 : static_cast<int>(item->itemID)),
                                   selected ? (g.highContrast ? GetSysColor(COLOR_HIGHLIGHTTEXT) : ui::accent(g.dark)) : secondaryColor());
        tr.left += px(49);
        DrawTextW(item->hDC, text, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
            RECT focus = item->rcItem;
            InflateRect(&focus, -px(5), -px(3));
            DrawFocusRect(item->hDC, &focus);
        }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        wchar_t cls[32]{};
        GetClassNameW((HWND)lp, cls, (int)std::size(cls));
        if (_wcsicmp(cls, L"Edit") == 0) {
            SetBkColor((HDC)wp, pageBg());
            SetTextColor((HDC)wp, secondaryColor());
            return (LRESULT)pageBrush();
        }
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)pageBrush();
    }
    case WM_CTLCOLOREDIT: {
        SetBkColor((HDC)wp, ui::palette(g.dark, g.highContrast).surface);
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)editBrush();
    }
    case WM_CTLCOLORBTN:
        SetBkMode((HDC)wp, TRANSPARENT);
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)pageBrush();
    case WM_CTLCOLORLISTBOX:
        SetBkColor((HDC)wp, navBg());
        SetTextColor((HDC)wp, textColor());
        return (LRESULT)navBrush();
    case WM_ERASEBKGND: {
        RECT rc{};
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, pageBrush());
        RECT nav{0, 0, px(184), rc.bottom};
        FillRect((HDC)wp, &nav, navBrush());
        return 1;
    }
    case WM_SETTINGCHANGE:
        g.dark = Settings::shared().useDarkMode();
        g.highContrast = ui::highContrastEnabled();
        applyTheme(hwnd);
        return 0;
    case WM_CLOSE:
        if (!leaveFeatureEditor()) return 0;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        if (g.font) { DeleteObject(g.font); g.font = nullptr; }
        if (g.fontBold) { DeleteObject(g.fontBold); g.fontBold = nullptr; }
        if (g.fontTitle) { DeleteObject(g.fontTitle); g.fontTitle = nullptr; }
        g.hwnd = nullptr;
        g.pages.clear();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

namespace SettingsWindow {

void refreshFeatures() {
    if (!g.hwnd) return;
    g.shortcutsReady = App::shared().shortcutsRegistered();
    updateFirstRunCompletion();
    for (int i = 0; i < 3; ++i) InvalidateRect(ctrl(kFeature0 + i), nullptr, TRUE);
    const auto& s = Settings::shared();
    std::wstring notice = L"Choose a feature to edit its shortcut, languages, provider and prompt.\nChanges in a feature are applied when you save.";
    if (!s.featureMigrationNotice.empty()) notice += L"\n\n" + util::Widen(s.featureMigrationNotice);
    SetWindowTextW(ctrl(kFeatureNotice), notice.c_str());
}
void openFeature(HWND appWindow, int feature) {
    open(appWindow);
    if (!g.hwnd || feature < 0 || feature > 2 || !leaveFeatureEditor()) return;
    SendMessageW(ctrl(kTab), LB_SETCURSEL, 2, 0); openFeatureEditor((Feature)feature);
}

bool recordingShortcut() { return g.hwnd && g.currentPage == 3 && GetFocus() == ctrl(kFeatureHotkey); }
bool processMessage(MSG* message) {
    if (!g.hwnd) return false;
    const bool handled = IsDialogMessageW(g.hwnd, message) != FALSE;
    if (handled && message->message == WM_KEYDOWN && message->wParam == VK_TAB) {
        HWND focused = GetFocus();
        if (focused && focused != ctrl(kTab) && IsChild(g.hwnd, focused)) {
            RECT r{}, client{}; GetWindowRect(focused, &r); MapWindowPoints(nullptr, g.hwnd, (POINT*)&r, 2);
            GetClientRect(g.hwnd, &client);
            int next = g.scrollY;
            if (r.top < px(12)) next += r.top - px(12);
            if (r.bottom > client.bottom - px(12)) next += r.bottom - client.bottom + px(12);
            SCROLLINFO info{sizeof(info), SIF_ALL}; GetScrollInfo(g.hwnd, SB_VERT, &info);
            if (next != g.scrollY) scrollPage(std::clamp(next, 0, std::max(0, info.nMax - (int)info.nPage + 1)));
        }
    }
    return handled;
}

void open(HWND appWindow) {
    if (g.hwnd) {
        ShowWindow(g.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g.hwnd);
        return;
    }
    g.appWindow = appWindow;
    g.dark = Settings::shared().useDarkMode();
    g.highContrast = ui::highContrastEnabled();
    enableCommonControlDarkMode();

    static bool registered = [] {
        INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES | ICC_HOTKEY_CLASS | ICC_LINK_CLASS |
                                                  ICC_STANDARD_CLASSES};
        InitCommonControlsEx(&icc);
        WNDCLASSW wc{};
        wc.lpfnWndProc = wndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(IDI_APPICON));
        wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
        RegisterClassW(&wc);
        return true;
    }();
    (void)registered;

    g.dpi = GetDpiForSystem();
    RECT work{}; SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int w = px(820), h = std::max(px(560), std::min(px(720), (int)(work.bottom - work.top) - px(64)));
    g.scrollY = 0;
    RECT wr{0, 0, w, h};
    AdjustWindowRect(&wr, WS_OVERLAPPEDWINDOW, FALSE);

    g.hwnd = CreateWindowExW(0, kClassName, L"TypeTide-Win Settings",
                             WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             CW_USEDEFAULT, CW_USEDEFAULT,
                             wr.right - wr.left, wr.bottom - wr.top,
                             nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g.hwnd) return;
    g.dark = Settings::shared().useDarkMode();
    g.highContrast = ui::highContrastEnabled();

    HICON smallIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
                                        IMAGE_ICON, px(16), px(16), LR_DEFAULTCOLOR | LR_SHARED);
    SendMessageW(g.hwnd, WM_SETICON, ICON_BIG,
                 (LPARAM)LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON)));
    SendMessageW(g.hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);

    // 字体
    NONCLIENTMETRICSW ncm{sizeof(ncm)};
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g.font = CreateFontIndirectW(&ncm.lfMessageFont);
    LOGFONTW bold = ncm.lfMessageFont;
    bold.lfWeight = FW_SEMIBOLD;
    bold.lfHeight = MulDiv(bold.lfHeight, 5, 4);
    g.fontBold = CreateFontIndirectW(&bold);
    LOGFONTW title = ncm.lfMessageFont;
    title.lfWeight = FW_SEMIBOLD;
    title.lfHeight = -MulDiv(20, (int)g.dpi, 72);
    g.fontTitle = CreateFontIndirectW(&title);

    g.loading = true;
    g.clientWidth = w;
    g.clientHeight = h;

    // Windows 11-style left navigation.
    HWND tab = CreateWindowExW(0, L"LISTBOX", L"",
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | LBS_NOTIFY |
                                   LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOINTEGRALHEIGHT,
                               0, px(14), px(184), h - px(28), g.hwnd,
                               (HMENU)kTab, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(tab, WM_SETFONT, (WPARAM)g.font, TRUE);
    SendMessageW(tab, LB_SETITEMHEIGHT, 0, px(42));
    const wchar_t* names[] = {L"General", L"Providers", L"Features",
                              L"Excluded apps", L"About & diagnostics"};
    for (int i = 0; i < 5; ++i) {
        SendMessageW(tab, LB_ADDSTRING, 0, (LPARAM)names[i]);
    }
    SendMessageW(tab, LB_SETCURSEL, 0, 0);

    buildPages();
    applyTheme(g.hwnd);
    showPage(0);
    g.loading = false;


    SetForegroundWindow(g.hwnd);
}

} // namespace SettingsWindow
