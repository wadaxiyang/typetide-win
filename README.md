<p align="center"><img src="docs/branding/typetide.svg" width="128" alt="TypeTide-Win icon"></p>

# TypeTide-Win

**System-wide AI translation and inline rewrite for Windows.**

[简体中文](README.zh-CN.md) · [Project](https://github.com/wadaxiyang/typetide-win) · [Releases](../../releases) · [Issues](https://github.com/wadaxiyang/typetide-win/issues)

TypeTide-Win is a Windows-only fork of [TypeTide by everettjf](https://github.com/everettjf/typetide).
It keeps the native **Win32 / C++20** implementation, with a refreshed transparent SVG wave icon.
The upstream project is credited as a reference under its original MIT license.

## Features

- **Read — Alt+D:** select text to display a complete translation beside it after a loading indicator.
- **Write — Alt+R:** translate and replace your input in place, preserving native Ctrl+Z undo.
- Optional floating selection icon and automatic translation.
- Copy, replace, or change the target language in the popup.
- Configurable shortcuts and smart/fixed language direction.
- Faithful, Formal, Casual, and Polished rewrite styles.
- Tray menu, launch at login, per-app exclusions, System / Light / Dark appearance, and local diagnostics.

Supports English, Chinese, Hindi, Spanish, French, Arabic, Bengali, Russian,
Portuguese, and Indonesian.

## Providers and privacy

In **Settings → Providers**, add named providers and select a row to edit, remove,
move up or move down. The app starts with #1 and tries the next entry after a
connection error, provider error, unfinished response or empty result. It stops
at the first complete translation. Cancelling stops the chain. A failed attempt's
partial text is discarded; fallback results do not prevent retrying #1 next time.

| API format | Base URL example | Request path added by the app |
| --- | --- | --- |
| Ollama | `http://127.0.0.1:11434` | `/api/generate` |
| OpenAI-compatible | `https://api.openai.com/v1` | `/chat/completions` |
| Anthropic | `https://api.anthropic.com/v1` | `/messages` |
| Gemini | `https://generativelanguage.googleapis.com/v1beta` | `/models/{model}:streamGenerateContent` |

Enter a model supported by your provider. Ollama models must be installed on the
configured server. OpenAI-compatible services include OpenRouter and DeepSeek.
**Test selected** checks only the selected entry with a fixed synthetic phrase,
without falling back to another provider or using your clipboard.

Local Ollama can work offline. Cloud providers receive selected text; an ordered
fallback chain may send that text to multiple configured services. Keep only local
providers in the list for local-only translation. Each provider's API key is stored
separately in Windows Credential Manager. Existing single-provider settings migrate
to the first list entry without losing the URL, model or key.

The sidebar groups settings into General, Providers, Translation (languages,
directions and styles), Shortcuts & triggers, Excluded apps, and About & diagnostics.

Selection capture uses UI Automation, with a temporary Ctrl+C fallback that
restores text clipboard contents afterward. This fallback does not preserve
non-text clipboard formats; clipboard-history tools may record temporary copies.
Local diagnostics track timings and outcomes without collecting user text.

## Install and use

Requires **Windows 10+ (x64)**. When published, download
`TypeTide-Win-Setup-x.y.z.exe` or `TypeTide-Win-Windows-x.y.z.zip`
from this fork's [Releases](../../releases), or build from source below.

1. Start TypeTide-Win and open Settings from the tray.
2. Add providers, set their priority and use **Test selected**.
3. Choose your native/foreign languages and verify the shortcuts.
4. Select text and press Alt+D to read or Alt+R to translate and replace it.

Settings and crash reports use `%APPDATA%\TypeTide-Win\`. This fork has its own
installer identity, startup entry and credentials, separate from upstream TypeTide.
Updates and downloads use **wadaxiyang/typetide-win**.

## Build and test

Install Visual Studio 2022 or later with **Desktop development with C++**,
including the Windows SDK, CMake 3.24+ and Ninja. In PowerShell at the repository root:

```powershell
.\windows\scripts\build.ps1
```

Output: `windows\build\TypeTide-Win.exe`.

Alternatively, in a Visual Studio Developer PowerShell:

```powershell
cmake -S windows -B windows/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build
```

Run built-in self-tests with isolated settings:

```powershell
$env:TYPETIDE_DATA_DIR = "$env:TEMP\typetide-win-selftest"
$p = Start-Process .\windows\build\TypeTide-Win.exe -ArgumentList '--selftest' -NoNewWindow -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Self-test failed: $($p.ExitCode)" }
Remove-Item Env:\TYPETIDE_DATA_DIR
```

Run the popup and loopback provider regression suites from a Developer PowerShell:

```powershell
ctest --test-dir windows/build --output-on-failure
```

These tests do not contact external model services.

See [Windows development](windows/README.md) and the [release test matrix](windows/TESTING.md)
for streaming and end-to-end tests. `contracts/translation-contracts.json`
retains regression fixtures for routing, prompts and streaming assembly.

## Packaging and releases

```powershell
# Optional version bump: default patch +1, or -Minor / -Major / -Version 1.2.3
.\scripts\bump-version.ps1

# Build, self-test, installer and portable ZIP (requires Inno Setup 6)
.\windows\scripts\build-release.ps1

# Push a version tag after committing release code; GitHub Actions builds and publishes
.\scripts\release-windows.ps1
```

SVG sources and generated native assets are checked in; normal builds do not require Python.
Regenerate vector geometry with `python scripts/generate-brand-assets.py`, then export
the transparent ICO with `windows/scripts/make-icon.ps1` (Windows System.Drawing).

## Credits and license

- Project: [wadaxiyang/typetide-win](https://github.com/wadaxiyang/typetide-win)
- Original author and upstream reference: [everettjf/typetide](https://github.com/everettjf/typetide)
- [MIT License](LICENSE), with the original copyright notice retained.
