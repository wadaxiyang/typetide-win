//
//  SettingsWindow.h — 设置窗口。
//  Pages: General / Providers / Translation / Shortcuts & triggers / Excluded apps / About.
//  Provider edits use Save/Cancel; other preferences apply immediately.
//
#pragma once
#include <windows.h>

namespace SettingsWindow {

/// 打开（或前置已打开的）设置窗口。appWindow 用于设置变更后通知重装配。
void open(HWND appWindow);
bool processMessage(MSG* message);

} // namespace SettingsWindow
