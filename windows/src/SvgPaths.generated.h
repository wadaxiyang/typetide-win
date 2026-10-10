// Generated from windows/assets/*.svg. Run scripts/generate-brand-assets.py.
#pragma once
#include <gdiplus.h>
namespace svgpaths {
inline void Brand(Gdiplus::GraphicsPath& p) {
    p.StartFigure();
    p.AddBezier(17.f, 48.f, 31.f, 48.f, 29.f, 9.f, 46.f, 10.f);
    p.AddBezier(46.f, 10.f, 63.f, 10.f, 65.f, 31.f, 51.f, 37.f);
    p.AddBezier(51.f, 37.f, 57.f, 28.f, 49.f, 23.f, 43.f, 28.f);
    p.AddBezier(43.f, 28.f, 36.f, 34.f, 43.f, 47.f, 60.f, 50.f);
    p.AddBezier(60.f, 50.f, 43.f, 58.f, 27.f, 56.f, 17.f, 48.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(4.5f, 26.5f, 5.f, 5.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(11.5f, 20.5f, 5.f, 5.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(20.f, 18.f, 4.f, 4.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(4.f, 37.f, 4.f, 4.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(11.5f, 30.5f, 7.f, 7.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(20.5f, 26.5f, 5.f, 5.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(6.5f, 45.5f, 5.f, 5.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(16.5f, 39.5f, 7.f, 7.f);
    p.CloseFigure();
}
inline void Navigation(Gdiplus::GraphicsPath& p, int index) {
    switch (index) {
    case 0: // general
    p.StartFigure();
    p.AddEllipse(6.f, 6.f, 12.f, 12.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(10.f, 10.f, 4.f, 4.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddLine(12.f, 2.f, 12.f, 5.f);
    p.StartFigure();
    p.AddLine(12.f, 19.f, 12.f, 22.f);
    p.StartFigure();
    p.AddLine(2.f, 12.f, 5.f, 12.f);
    p.StartFigure();
    p.AddLine(19.f, 12.f, 22.f, 12.f);
    p.StartFigure();
    p.AddLine(5.f, 5.f, 7.f, 7.f);
    p.StartFigure();
    p.AddLine(17.f, 17.f, 19.f, 19.f);
    p.StartFigure();
    p.AddLine(5.f, 19.f, 7.f, 17.f);
    p.StartFigure();
    p.AddLine(17.f, 7.f, 19.f, 5.f);
        break;
    case 1: // backend
    p.StartFigure();
    p.AddLine(4.f, 3.f, 20.f, 3.f);
    p.AddLine(20.f, 3.f, 20.f, 10.f);
    p.AddLine(20.f, 10.f, 4.f, 10.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddLine(4.f, 14.f, 20.f, 14.f);
    p.AddLine(20.f, 14.f, 20.f, 21.f);
    p.AddLine(20.f, 21.f, 4.f, 21.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddLine(8.f, 6.f, 8.f, 7.f);
    p.StartFigure();
    p.AddLine(8.f, 17.f, 8.f, 18.f);
    p.StartFigure();
    p.AddLine(12.f, 7.f, 16.f, 7.f);
    p.StartFigure();
    p.AddLine(12.f, 18.f, 16.f, 18.f);
        break;
    case 2: // language
    p.StartFigure();
    p.AddEllipse(3.f, 3.f, 18.f, 18.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddLine(3.f, 12.f, 21.f, 12.f);
    p.StartFigure();
    p.AddBezier(12.f, 3.f, 5.f, 8.f, 5.f, 16.f, 12.f, 21.f);
    p.AddBezier(12.f, 21.f, 19.f, 16.f, 19.f, 8.f, 12.f, 3.f);
    p.CloseFigure();
        break;
    case 3: // shortcuts
    p.StartFigure();
    p.AddLine(3.f, 5.f, 21.f, 5.f);
    p.AddLine(21.f, 5.f, 21.f, 19.f);
    p.AddLine(21.f, 19.f, 3.f, 19.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddLine(7.f, 9.f, 8.f, 9.f);
    p.StartFigure();
    p.AddLine(12.f, 9.f, 13.f, 9.f);
    p.StartFigure();
    p.AddLine(17.f, 9.f, 18.f, 9.f);
    p.StartFigure();
    p.AddLine(7.f, 12.f, 8.f, 12.f);
    p.StartFigure();
    p.AddLine(12.f, 12.f, 13.f, 12.f);
    p.StartFigure();
    p.AddLine(17.f, 12.f, 18.f, 12.f);
    p.StartFigure();
    p.AddLine(8.f, 16.f, 16.f, 16.f);
        break;
    case 4: // behavior
    p.StartFigure();
    p.AddLine(3.f, 5.f, 8.f, 5.f);
    p.StartFigure();
    p.AddLine(12.f, 5.f, 21.f, 5.f);
    p.StartFigure();
    p.AddLine(3.f, 12.f, 14.f, 12.f);
    p.StartFigure();
    p.AddLine(18.f, 12.f, 21.f, 12.f);
    p.StartFigure();
    p.AddLine(3.f, 19.f, 6.f, 19.f);
    p.StartFigure();
    p.AddLine(10.f, 19.f, 21.f, 19.f);
    p.StartFigure();
    p.AddEllipse(8.f, 3.f, 4.f, 4.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(14.f, 10.f, 4.f, 4.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddEllipse(6.f, 17.f, 4.f, 4.f);
    p.CloseFigure();
        break;
    case 5: // excluded
    p.StartFigure();
    p.AddLine(3.f, 7.f, 17.f, 7.f);
    p.AddLine(17.f, 7.f, 17.f, 21.f);
    p.AddLine(17.f, 21.f, 3.f, 21.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddLine(7.f, 3.f, 21.f, 3.f);
    p.AddLine(21.f, 3.f, 21.f, 17.f);
    p.StartFigure();
    p.AddLine(7.f, 11.f, 13.f, 17.f);
    p.StartFigure();
    p.AddLine(13.f, 11.f, 7.f, 17.f);
        break;
    case 6: // diagnostics
    p.StartFigure();
    p.AddLine(2.f, 12.f, 6.f, 12.f);
    p.AddLine(6.f, 12.f, 9.f, 4.f);
    p.AddLine(9.f, 4.f, 14.f, 20.f);
    p.AddLine(14.f, 20.f, 17.f, 12.f);
    p.AddLine(17.f, 12.f, 22.f, 12.f);
        break;
    case 7: // about
    p.StartFigure();
    p.AddEllipse(3.f, 3.f, 18.f, 18.f);
    p.CloseFigure();
    p.StartFigure();
    p.AddLine(12.f, 7.f, 12.f, 7.2f);
    p.StartFigure();
    p.AddLine(12.f, 11.f, 12.f, 17.f);
        break;
    }
}
} // namespace svgpaths
