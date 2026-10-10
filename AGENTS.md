# TypeTide-Win — Agent Guide

Windows-only system-wide **AI translation + inline rewrite**, implemented as a
native Win32 C++20 tray app with CMake and no third-party runtime dependencies.

- Project: https://github.com/wadaxiyang/typetide-win
- Upstream reference: https://github.com/everettjf/typetide (everettjf)
- Preserve the upstream MIT copyright. Icon source: `windows/assets/brand.svg`;
  regenerate native paths and transparent ICO after changing the SVG.

Read: select text → shortcut / floating icon / auto → translation popup.
Write: type in your native language → shortcut → translated and replaced in place.

```
Shortcut / SelectionMonitor → SelectionCapture → Translator → provider
                                  Popup / TextReplacer ← streamed result
```

Backends: Ollama (default `http://127.0.0.1:11434`) or any OpenAI-compatible
`/chat/completions` endpoint with SSE streaming. Ten languages, configurable
native/foreign direction, and four rewrite styles.

## Windows (`windows/`)

Pure Win32 + WinHTTP + UI Automation, C++20, CMake + Ninja, zero third-party
dependencies (tiny JSON parser included in `src/Json.h`).

- `src/App.*` — orchestrator, owns the message-only window that receives hotkeys and worker notifications.
- `src/SelectionCapture.*` — UI Automation TextPattern selection + synthetic Ctrl+C clipboard fallback (clipboard always restored); also returns the selection's screen rect for popup anchoring.
- `src/TextReplacer.*` — synthetic Ctrl+V paste (undo-safe), clipboard restored after.
- `src/Translator.*` — ordered provider routing over WinHTTP; `ProviderProtocol.*` builds requests and parses Ollama NDJSON / OpenAI / Anthropic / Gemini SSE; in-memory LRU cache. Ollama requests send `"think": false` (retry without on 400) — thinking models (qwen3 family) otherwise stall for minutes on hidden reasoning.
- `src/OllamaModels.*` — legacy model discovery and primary Ollama preload. Managed provider entries are never silently rewritten.
- `src/Language.*` — 10 languages, prompts, detection (script ranges + Latin stopword scoring).
- `src/PopupWindow.*` — translation popup with loading state and atomic result display (Copy / Replace, Esc / click-outside dismiss).
- `src/SelectionIcon.*` / `src/SelectionMonitor.*` — floating icon & auto-translate triggers (WH_MOUSE_LL).
- `src/SettingsWindow.*` — native six-page sidebar: General, Providers, Translation, Shortcuts & triggers, Excluded apps, About & diagnostics. Provider edits use Save/Cancel; priority moves save immediately.
- `src/Settings.*` — JSON settings at `%APPDATA%\TypeTide-Win\settings.json`.
- `src/TrayIcon.*`, `src/Hotkeys.*`, `src/LaunchAtLogin.*` (HKCU Run key), `src/UpdateChecker.*`.

### Conventions & gotchas

- All UI on the main thread, and the main thread NEVER sleeps (a stalled message loop gets WH_*_LL hooks silently removed). Waiting work (clipboard-fallback capture, paste replace, HTTP) lives on worker threads posting `WM_APP_*` messages with heap payloads (receiver frees).
- Workers must not read `Settings::shared()` — snapshot config on the main thread and pass it in (see `StreamWithProviders` and `FetchInstalledAsync`).
- Paste replace uses clipboard delayed rendering (WM_RENDERFORMAT = "target consumed the paste"); settings saves are atomic (tmp+rename); crashes dump minidumps to `%APPDATA%\TypeTide-Win\crashes`.
- `RegisterHotKey` fails if another app owns the combo — surface it, don't crash.

### Build

```powershell
cd windows
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

## Releases

The root `VERSION` file is the version source. Run `scripts/bump-version.ps1`
(default patch +1, `-Minor`, `-Major`, or `-Version 1.2.3`) to sync
`windows/CMakeLists.txt`, `windows/src/TypeTide-Win.rc`, and the app's own
assemblyIdentity in `windows/src/TypeTide-Win.manifest`. The Common-Controls
`version="6.0.0.0"` must stay unchanged or the executable fails to start.

`scripts/release-windows.ps1` builds via `windows/scripts/build-release.ps1`
(Release executable, self-test gate, Inno Setup installer, portable ZIP), then
publishes to `wadaxiyang/typetide-win`. It requires `gh auth login`.
UpdateChecker reads this fork's `releases/latest` endpoint.

Keep README files free of pinned app release versions. Link to `../../releases`
and use placeholder names such as `TypeTide-Win-Setup-x.y.z.exe`.

## Current Product Priorities

- Use **TypeTide-Win** consistently in UI, package names, metadata, and documentation.
- Keep project/update/support links on the fork; label upstream links as references.
- Track capture/replacement success, first-token/total latency, cancellation and
  provider failure locally during development without collecting user text.
- Keep Ollama a first-class offline path and make network-provider use obvious
  before selected text leaves the device.
- Maintain regression tests for language direction, detection, prompts, streaming
  assembly, and clipboard restoration. Keep `contracts/translation-contracts.json`.
