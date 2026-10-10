# TypeTide-Win — Windows development

Native Win32 C++20 system-wide AI translation and inline rewrite. Uses WinHTTP,
UI Automation and a built-in JSON parser, with no third-party runtime dependencies.

- Project: [wadaxiyang/typetide-win](https://github.com/wadaxiyang/typetide-win)
- Upstream reference: [everettjf/typetide](https://github.com/everettjf/typetide)

## Features and configuration

Translate selected text with Alt+D, rewrite in place with Alt+R, or enable the
floating selection icon and automatic translation. Results are collected behind a loading indicator, then shown in a native
popup with Copy, Replace, and target-language controls. The app includes a system
tray menu, launch at login, per-app exclusions, light/dark mode, and local diagnostics.

Settings persist to `%APPDATA%\TypeTide-Win\settings.json`
(override dir with the `TYPETIDE_DATA_DIR` env var; set `TYPETIDE_DEBUG=1` to
write `debug.log` beside it).

Add named providers with OpenAI-compatible, Anthropic, Gemini or Ollama format.
Order them with Move up/down; a failed attempt falls back to the next entry.
API keys are stored separately in Windows Credential Manager, outside settings.json.
A primary Ollama model is preloaded and kept warm for 10 minutes.

The shortcut editor supports Ctrl, Shift, Alt, and the Windows key. Each of the
two shortcuts independently chooses smart/native/foreign direction and popup or
in-place replacement. Either shortcut can be cleared; keep at least one configured
for keyboard triggering. Configure an installed Ollama model explicitly; managed
provider entries are never silently changed by model discovery.

## Build

Requirements: Windows 10+, Visual Studio 2022+ (MSVC toolset, CMake, Ninja —
all bundled with the "Desktop development with C++" workload).

```powershell
# from a VS Developer PowerShell / Command Prompt
cd windows
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build          # → build\TypeTide-Win.exe
```

The transparent icon is authored in `assets/brand.svg`; navigation symbols live in
`assets/navigation.svg`. Regenerate the checked-in GDI+ geometry with
`python ../scripts/generate-brand-assets.py` and export the ICO using
`scripts\make-icon.ps1`. These are development tools; the executable only uses Windows APIs.

From a regular PowerShell at the repository root, `./windows/scripts/build.ps1`
automatically locates the installed Visual Studio C++ toolchain.

## Test

```powershell
# unit-style self-tests (JSON, language detection, settings, translation contracts,
# privacy-safe diagnostics, prompts)
build\TypeTide-Win.exe --selftest

# + a real streaming translation through the configured backend
build\TypeTide-Win.exe --selftest-translate
```

End-to-end (drives the real app + a mock backend + a scratch editor window;
uses synthetic input, so keep hands off the keyboard while it runs):

```powershell
# terminal 1 — mock backend (OpenAI SSE + Ollama NDJSON on :8199)
scripts\mock-openai.ps1

# terminal 2 — point the app at a scratch data dir with mock settings, then:
$env:TYPETIDE_DATA_DIR = "$env:TEMP\typetide-e2e"
scripts\test-e2e.ps1        # WinForms host → clipboard-fallback capture path
scripts\test-e2e-wpf.ps1    # WPF host → UI Automation TextPattern path
```

For the complete release-candidate matrix (hotkey conflicts, first-run checks,
dark mode, clipboard restoration, cloud/Ollama streaming, and installer smoke
tests), follow [`TESTING.md`](TESTING.md) on a Windows machine.

## Architecture

```
main.cpp             single instance, COM init, message loop
App                  orchestrator: hotkeys, flows, thread marshaling (WM_APP_*)
SelectionCapture     UIA TextPattern selection (+ rect) → synthetic Ctrl+C fallback
TextReplacer         clipboard paste replace (undo-safe), clipboard restored
Translator           prompts · ordered provider fallback · LRU cache (worker threads)
ProviderProtocol     Ollama / OpenAI / Anthropic / Gemini request and stream formats
OllamaModels         legacy model discovery · async primary preload/keep-alive
Http                 WinHTTP streaming POST / GET
PopupWindow          no-activate topmost popup: loading then complete text, Copy/Replace,
                     retarget menu, Esc / click-outside dismiss, dark mode, DPI
SelectionIcon        floating icon next to selection
SelectionMonitor     WH_MOUSE_LL mouse-up → UIA selection (icon / auto trigger)
SettingsWindow       six-page sidebar · ordered provider list with Save/Cancel editor
TrayIcon / Hotkeys / LaunchAtLogin / UpdateChecker / Settings / Json / Language
```

Threading rule: all UI on the main thread, and the main thread **never
sleeps** — a stalled message loop gets the low-level hooks silently removed
by the OS. Anything that waits (clipboard-fallback capture, paste
replacement, HTTP) runs on worker threads and posts `WM_APP_*` messages with
heap payloads (receiver frees). Workers never read `Settings::shared()`
directly; config is snapshotted on the main thread and passed in.

Reliability notes:
- Paste replacement uses clipboard delayed rendering (`WM_RENDERFORMAT`) to
  know when the target actually consumed the paste before restoring the
  original clipboard.
- `settings.json` writes are atomic (temp file + rename).
- Unhandled exceptions write a minidump to `%APPDATA%\TypeTide-Win\crashes\`
  (the 5 most recent are kept) — attach one when reporting a crash.
- The tray icon re-registers itself after an explorer.exe restart.
- Selections over 5000 characters are rejected with a friendly message.

Thinking models (qwen3 family etc.) are handled: the Ollama request sends
`"think": false` (and retries without it for models that reject the field) —
otherwise a 2-second translation silently burns minutes on hidden reasoning.

## Known limitations

- Some apps (many Electron apps, some WinForms controls) don't expose UIA
  TextPattern — capture falls back to an invisible copy and the popup anchors
  at the cursor instead of the selection.
- `RegisterHotKey` fails if another app owns the combo; TypeTide-Win warns once and
  you can pick a different shortcut in Settings → Shortcuts.

## UI regression checks

CMake builds `TypeTide-UiTests.exe` when `BUILD_TESTING` is enabled. Run
`ctest --test-dir build --output-on-failure` to verify loading/completion behavior,
partial-response isolation, retry, empty/error output, long text, and dismissal.
For a manual loading-to-result preview, run `build\TypeTide-UiTests.exe --preview`.
Choose General → Appearance in the app to compare System / Light / Dark without
changing Windows settings.


## Provider configuration

Settings → Providers contains an ordered, persisted list. Add/edit uses Save and
Cancel; Move up/down changes priority immediately. Supported wire formats are
Ollama generate, OpenAI chat completions, Anthropic Messages, and Gemini native
Generate Content. Each provider has its own name, base URL, model, and credential
entry. Existing single-backend settings are migrated lazily.

`ProviderProtocol.*` owns request construction, endpoint validation and stream
completion parsing; `Translator.*` snapshots the list before starting its worker,
buffers each attempt and emits only the winning complete result. Incomplete,
empty or failed responses advance to the next entry. Cancellation does not.
Fallback results are not cached so the primary can recover on the next request.
Use `ctest --test-dir windows/build --output-on-failure` for offline regressions.
