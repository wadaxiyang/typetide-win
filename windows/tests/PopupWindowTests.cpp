#include "PopupWindow.h"
#include "Settings.h"
#include <windows.h>
#include <objbase.h>
#include <cstdio>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
HWND popupWindow() { return FindWindowW(L"TypeTide-WinPopup", nullptr); }
std::wstring visibleText() {
    wchar_t text[16384]{};
    SendMessageW(popupWindow(), WM_GETTEXT, std::size(text), (LPARAM)text);
    return text;
}
RECT bounds() { RECT r{}; GetWindowRect(popupWindow(), &r); return r; }
constexpr RECT anchor{360, 180, 450, 210};
constexpr wchar_t result[] = L"A calmer way to translate.\n\nYour complete translation appears at once, with no moving text or particle effects. Copy it, replace the original, or choose another language.";
}

int main(int argc, char**) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    auto& popup = PopupWindow::shared();
    popup.show(L"Original selected text", Language::English, anchor, true);
    if (argc > 1) {
        // Manual visual preview: a real window, with one simulated delayed result.
        // Expose the preview in the taskbar so desktop inspection tools can
        // select it; production popups remain non-activating tool windows.
        const HWND preview = popupWindow();
        ShowWindow(preview, SW_HIDE);
        const auto style = GetWindowLongPtrW(preview, GWL_EXSTYLE);
        SetWindowLongPtrW(preview, GWL_EXSTYLE,
            (style & ~(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) | WS_EX_APPWINDOW);
        ShowWindow(preview, SW_SHOWNOACTIVATE);
        SetTimer(popupWindow(), 50, 18000, [](HWND hwnd, UINT, UINT_PTR id, DWORD) {
            KillTimer(hwnd, id);
            auto& p = PopupWindow::shared();
            p.appendDelta(result);
            p.finishTranslation();
        });
        popup.onClosed = [] { PostQuitMessage(0); };
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        CoUninitialize();
        return 0;
    }
    check(popupWindow() != nullptr, "native popup opens");
    RECT loading = bounds();
    popup.appendDelta(L"  First half ");
    popup.appendDelta(L"and second half.  ");
    check(popup.translation().empty() && visibleText().empty(), "partial result is not exposed");
    RECT pending = bounds();
    check(EqualRect(&loading, &pending), "incoming tokens do not resize the loading card");
    popup.finishTranslation();
    check(visibleText() == L"First half and second half.", "completion reveals the full result once");
    RECT completed = bounds();
    check(completed.bottom - completed.top > loading.bottom - loading.top, "completion expands the card");
    popup.appendDelta(L"late token");
    check(visibleText() == L"First half and second half.", "completed result ignores late deltas");
    popup.resetForRetranslate();
    check(visibleText().empty(), "retarget clears the previous result");
    popup.appendDelta(L"incomplete response");
    popup.setError(L"Connection interrupted");
    check(popup.translation().empty() && visibleText().empty(), "failure discards incomplete output");
    popup.finishTranslation();
    check(visibleText().empty(), "failure cannot be committed by a late completion");
    popup.resetForRetranslate();
    popup.appendDelta(L" \n\t");
    popup.finishTranslation();
    check(popup.translation().empty(), "empty provider output stays an error");
    popup.resetForRetranslate();
    std::wstring longText;
    for (int i = 0; i < 90; ++i) longText += L"Long translations remain readable and scrollable.\n";
    popup.appendDelta(longText);
    popup.finishTranslation();
    check(visibleText().size() > 4000, "long translation stays complete");
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromWindow(popupWindow(), MONITOR_DEFAULTTONEAREST), &monitor);
    RECT large = bounds();
    check(large.top >= monitor.rcWork.top && large.bottom <= monitor.rcWork.bottom, "long result fits the monitor work area");
    int closed = 0;
    popup.onClosed = [&] { ++closed; };
    popup.close();
    popup.close();
    check(closed == 1 && !popup.isVisible(), "close cancels once and is safe to repeat");
    popup.onClosed = nullptr;
    CoUninitialize();
    return failures ? 1 : 0;
}
