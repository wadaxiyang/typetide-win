//
//  LaunchAtLogin.h — 开机自启（HKCU Run 键）。
//
#pragma once

namespace launchatlogin {

bool IsEnabled();
void SetEnabled(bool enabled);

} // namespace launchatlogin
