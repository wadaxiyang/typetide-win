#pragma once
#include "Settings.h"
#include "Util.h"
#include <commctrl.h>

// Shared by feature settings and the input window; item data is the stable value.
namespace featureui {
inline void addChoice(HWND combo, const std::wstring& text, int value) {
    auto i = SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
    SendMessageW(combo, CB_SETITEMDATA, i, value);
}
inline void selectValue(HWND combo, int value) {
    for (int i = 0; i < SendMessageW(combo, CB_GETCOUNT, 0, 0); ++i)
        if ((int)SendMessageW(combo, CB_GETITEMDATA, i, 0) == value) {
            SendMessageW(combo, CB_SETCURSEL, i, 0); return;
        }
}
inline int value(HWND combo) {
    return (int)SendMessageW(combo, CB_GETITEMDATA, SendMessageW(combo, CB_GETCURSEL, 0, 0), 0);
}
inline void languages(HWND combo, int selected, bool source) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const auto& s = Settings::shared();
    for (int value : {kLanguageAuto, kLanguageNative, kLanguageForeign})
        addChoice(combo, s.languageLabel(value, source), value);
    if (!source) addChoice(combo, s.languageLabel(kLanguageSame), kLanguageSame);
    for (auto language : lang::All) addChoice(combo, lang::DisplayName(language), (int)language);
    selectValue(combo, selected);
}
inline void providers(HWND combo, const FeatureConfig& config) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    addChoice(combo, L"Follow default provider order", -1);
    int selected = -1;
    const auto list = Settings::shared().effectiveProviders();
    for (size_t i = 0; i < list.size(); ++i) {
        addChoice(combo, util::Widen(list[i].name + " · " + list[i].model), (int)i);
        if (list[i].id == config.providerId) selected = (int)i;
    }
    if (Settings::shared().providerMissing(config)) {
        addChoice(combo, L"Provider removed — following default", -2); selected = -2;
    }
    selectValue(combo, selected);
}
inline std::wstring text(HWND control) {
    std::wstring result(GetWindowTextLengthW(control) + 1, L'\0');
    result.resize(GetWindowTextW(control, result.data(), (int)result.size()));
    return result;
}
}
