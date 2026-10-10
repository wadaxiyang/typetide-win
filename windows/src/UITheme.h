#pragma once

#include <windows.h>

namespace ui {

// TypeTide-Win brand and semantic colors. Native Win32 controls
// retain system behavior while custom-drawn surfaces share these tokens.
constexpr COLORREF Accent = RGB(0x7C, 0x5C, 0xFF);
constexpr COLORREF AccentText = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF Success = RGB(0x20, 0x9B, 0x4B);
constexpr COLORREF WarningLight = RGB(0xD9, 0x77, 0x06);
constexpr COLORREF WarningDark = RGB(0xFF, 0xA5, 0x4C);
constexpr COLORREF DangerLight = RGB(0xC4, 0x2B, 0x1C);
constexpr COLORREF DangerDark = RGB(0xFF, 0x6B, 0x62);

inline COLORREF accent(bool dark, bool highContrast = false) {
    return highContrast ? GetSysColor(COLOR_HIGHLIGHT) : dark ? RGB(0xB2, 0xA2, 0xFF) : RGB(0x65, 0x59, 0xD8);
}

class SolidBrush {
public:
    ~SolidBrush() { if (handle_) DeleteObject(handle_); }
    HBRUSH get(COLORREF color) {
        if (!handle_ || color != color_) {
            if (handle_) DeleteObject(handle_);
            handle_ = CreateSolidBrush(color);
            color_ = color;
        }
        return handle_;
    }
private:
    HBRUSH handle_ = nullptr;
    COLORREF color_ = 0;
};

namespace spacing {
constexpr int XSmall = 4;
constexpr int Small = 8;
constexpr int Medium = 12;
constexpr int Large = 16;
constexpr int XLarge = 24;
constexpr int Page = 28;
}

namespace radius {
constexpr int Control = 8;
constexpr int Card = 10;
constexpr int Popup = 12;
}

namespace control {
constexpr int CompactHeight = 28;
constexpr int MinimumHitSize = 28;
constexpr int PopupMinWidth = 360;
constexpr int PopupIdealWidth = 400;
constexpr int PopupMaxWidth = 520;
}

struct Palette {
    COLORREF background;
    COLORREF surface;
    COLORREF text;
    COLORREF secondary;
    COLORREF divider;
    COLORREF button;
    COLORREF buttonBorder;
    COLORREF warning;
    COLORREF danger;
};

inline Palette palette(bool dark, bool highContrast = false) {
    if (highContrast) {
        return {GetSysColor(COLOR_WINDOW), GetSysColor(COLOR_WINDOW),
                GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_GRAYTEXT),
                GetSysColor(COLOR_WINDOWTEXT), GetSysColor(COLOR_BTNFACE),
                GetSysColor(COLOR_BTNTEXT), GetSysColor(COLOR_HIGHLIGHT),
                GetSysColor(COLOR_HIGHLIGHT)};
    }
    if (dark) {
        return {RGB(0x19, 0x20, 0x2D), RGB(0x21, 0x2A, 0x3A),
                RGB(0xEF, 0xF3, 0xFC), RGB(0xB2, 0xBF, 0xD5),
                RGB(0x36, 0x43, 0x5A), RGB(0x2C, 0x38, 0x4D),
                RGB(0x4C, 0x5D, 0x78), WarningDark, DangerDark};
    }
    return {RGB(0xF6, 0xF8, 0xFC), RGB(0xFF, 0xFF, 0xFF),
            RGB(0x24, 0x30, 0x46), RGB(0x60, 0x6E, 0x84),
            RGB(0xE2, 0xE7, 0xF0), RGB(0xF0, 0xF3, 0xFA),
            RGB(0xCF, 0xD8, 0xE7), WarningLight, DangerLight};
}

inline bool highContrastEnabled() {
    HIGHCONTRASTW contrast{sizeof(contrast)};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
           (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
}

} // namespace ui
