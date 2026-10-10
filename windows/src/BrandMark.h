#pragma once
#include "VectorIcons.h"

namespace brand {
// Same transparent SVG silhouette for popup, selection button, tray and installer.
inline void DrawMark(HDC dc, RECT bounds, COLORREF color, bool gradient = false) {
    if (!vectoricons::Initialize()) return;
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.TranslateTransform((float)bounds.left, (float)bounds.top);
    graphics.ScaleTransform((bounds.right - bounds.left) / 64.f, (bounds.bottom - bounds.top) / 64.f);
    Gdiplus::GraphicsPath path;
    svgpaths::Brand(path);
    if (gradient) {
        Gdiplus::LinearGradientBrush brush(Gdiplus::PointF(8, 12), Gdiplus::PointF(58, 54),
            Gdiplus::Color(255, 36, 214, 203), Gdiplus::Color(255, 167, 139, 250));
        Gdiplus::Color colors[] = {Gdiplus::Color(255, 36, 214, 203),
            Gdiplus::Color(255, 76, 166, 255), Gdiplus::Color(255, 167, 139, 250)};
        Gdiplus::REAL positions[] = {0.f, .5f, 1.f};
        brush.SetInterpolationColors(colors, positions, 3);
        graphics.FillPath(&brush, &path);
    } else {
        Gdiplus::SolidBrush brush(vectoricons::Color(color));
        graphics.FillPath(&brush, &path);
    }
}
} // namespace brand
