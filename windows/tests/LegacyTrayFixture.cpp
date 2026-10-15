#include <windows.h>

// Reproduce <= 0.2.1: a hidden tray host that acknowledges shutdown queries but
// ignores WM_ENDSESSION and keeps its message loop alive after WM_CLOSE.
LRESULT CALLBACK legacyProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    WNDCLASSW wc{};
    wc.hInstance = instance; wc.lpfnWndProc = legacyProc;
    wc.lpszClassName = L"TypeTide-LegacyTrayFixture";
    RegisterClassW(&wc);
    if (!CreateWindowW(wc.lpszClassName, L"TypeTide legacy upgrade fixture", 0,
        0, 0, 0, 0, nullptr, nullptr, instance, nullptr)) return 1;
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return 0;
}
