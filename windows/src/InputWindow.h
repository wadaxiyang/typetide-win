#pragma once
#include <windows.h>
#include "Settings.h"

constexpr UINT WM_APP_INPUT_DELTA = WM_APP + 20;
constexpr UINT WM_APP_INPUT_DONE = WM_APP + 21;
namespace InputWindow {
void open(HWND appWindow);
bool processMessage(MSG* message);
void receive(UINT message, WPARAM request, LPARAM payload);
void refreshTheme();
}
