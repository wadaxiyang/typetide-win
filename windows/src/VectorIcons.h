#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include "SvgPaths.generated.h"

namespace vectoricons {
// GDI+ is a Windows system component. SVG geometry is compiled ahead of time;
// drawing stays vector-based at every DPI and needs no runtime SVG dependency.
inline bool Initialize() {
    struct Runtime {
        ULONG_PTR token = 0;
        Runtime() { Gdiplus::GdiplusStartupInput input; Gdiplus::GdiplusStartup(&token, &input, nullptr); }
        ~Runtime() { if (token) Gdiplus::GdiplusShutdown(token); }
    };
    static Runtime runtime;
    return runtime.token != 0;
}
inline Gdiplus::Color Color(COLORREF c) {
    return Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c));
}
inline void DrawPath(HDC dc, RECT bounds, int index, COLORREF color, bool action) {
    if (!Initialize()) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.TranslateTransform((float)bounds.left, (float)bounds.top);
    graphics.ScaleTransform((bounds.right - bounds.left) / 24.f, (bounds.bottom - bounds.top) / 24.f);
    Gdiplus::GraphicsPath path;
    if (action) svgpaths::Action(path, index);
    else svgpaths::Navigation(path, index);
    Gdiplus::Pen pen(Color(color), 1.7f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    pen.SetLineJoin(Gdiplus::LineJoinRound);
    graphics.DrawPath(&pen, &path);
}
inline void DrawNavigation(HDC dc, RECT bounds, int index, COLORREF color) {
    DrawPath(dc, bounds, index, color, false);
}
enum class Action { Copy, Exchange, Check };
inline void DrawAction(HDC dc, RECT bounds, Action action, COLORREF color) {
    DrawPath(dc, bounds, static_cast<int>(action), color, true);
}
} // namespace vectoricons
