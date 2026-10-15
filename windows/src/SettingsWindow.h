//
//  SettingsWindow.h — 设置窗口。
//  Pages: General / Providers / Features / Excluded apps / About.
//  Provider and feature edits use Save/Cancel; general preferences apply immediately.
//
#pragma once
#include <windows.h>

namespace SettingsWindow {

/// 打开（或前置已打开的）设置窗口。appWindow 用于设置变更后通知重装配。
void open(HWND appWindow);
bool processMessage(MSG* message);
bool recordingShortcut();
void refreshFeatures();
void openFeature(HWND appWindow, int feature);

} // namespace SettingsWindow
