<p align="center"><img src="docs/branding/typetide.svg" width="128" alt="TypeTide-Win 图标"></p>

# TypeTide-Win

**Windows 原生的系统级 AI 翻译与原地改写工具。**

[English](README.md) · [本项目](https://github.com/wadaxiyang/typetide-win) · [下载](../../releases) · [反馈问题](https://github.com/wadaxiyang/typetide-win/issues)

本项目 fork 自 [everettjf 的 TypeTide](https://github.com/everettjf/typetide)，现仅支持 Windows，
继续使用 **Win32 / C++20**，以原波浪为灵感，使用透明 SVG 图标。原作者及上游地址保留为来源参考，并保留 MIT 版权声明。

## 主要功能

- **阅读 — Alt+D：** 选中文字，先显示加载提示，完成后在选区旁一次展示完整译文。
- **输入翻译 — Alt+Q：** 打开输入／译文窗口，默认母语 → 常用外语。Ctrl+Enter 翻译，中间 ⇄ 交换语言，一键复制结果；点击外部不会关闭，Esc 隐藏并保留本次运行中的草稿。
- **写作 — Alt+R：** 将输入内容翻译并原地替换，通过系统粘贴保留应用原生的 Ctrl+Z 撤销能力。
- 可开启划词浮动图标或选中后自动翻译。
- 在翻译弹窗中复制、替换或切换目标语言。
- 自定义快捷键、智能或固定翻译方向，支持忠实、正式、随意、润色四种风格。
- 系统托盘、开机自启、跳过指定应用、可选跟随系统、浅色或深色外观，以及本地诊断。

支持英语、中文、印地语、西班牙语、法语、阿拉伯语、孟加拉语、俄语、葡萄牙语和印尼语。

## Providers 与隐私

在 **设置 → Providers** 中添加多个服务。选中一项后可编辑、删除，或通过 **Move up / Move down**
调整优先级。每次从第 1 项开始，连接失败、服务报错、响应不完整或为空时自动尝试下一项；
完整成功后停止。失败服务的半截译文会丢弃，取消请求不会触发后续服务。
备用服务成功后，下次请求仍会重新尝试首选服务。

| API 格式 | Base URL 示例 | 应用自动追加的路径 |
| --- | --- | --- |
| Ollama | `http://127.0.0.1:11434` | `/api/generate` |
| OpenAI-compatible | `https://api.openai.com/v1` | `/chat/completions` |
| Anthropic | `https://api.anthropic.com/v1` | `/messages` |
| Gemini | `https://generativelanguage.googleapis.com/v1beta` | `/models/{model}:streamGenerateContent` |

模型填写对应服务支持的模型 ID，Ollama 使用该服务器已安装的模型。OpenAI 兼容格式也适用于
OpenRouter、DeepSeek 等服务。**Test selected** 只测试当前选中的一项，使用固定示例文字，
不读取剪贴板，也不会回退到其他服务。

本机 Ollama 可离线使用。云端服务会接收选中文字；自动回退可能把同一段文字发送给列表中的
多个服务。若希望正文只留在本机，列表中只保留本地服务。每项服务的 API Key 独立保存在
Windows 凭据管理器中。旧版单服务配置会迁移为第一项，保留原有地址、模型及密钥。

侧边栏为：常规、Providers、**Features**、排除应用、关于与诊断。母语／常用外语在常规中设置一次；
三个功能分别设置快捷键、源语言、目标规则、Provider、风格和提示词，详情页使用保存／取消。
提示词支持 `{source}`、`{target}`、`{style}`，可恢复默认；“保持原语言”支持同语言润色。

三个功能默认跟随 Provider 优先级，也可指定服务。指定服务被删除后回退默认，状态在设置中显示。
Provider 选择与路由说明统一放在设置页，翻译弹窗只呈现文字和语言操作。
Features 列表显示各快捷键的可用／冲突状态。输入窗口临时切换语言不修改功能默认设置。

取词优先使用 UI Automation，必要时临时模拟 Ctrl+C，并在完成后恢复文本剪贴板。
该回退路径不保留非文本剪贴板格式，剪贴板历史工具也可能记录临时复制的内容。
本地开发诊断记录耗时和操作结果，不收集用户正文。

## 安装使用

要求 **Windows 10+（x64）**。本项目发布后，可从 [Releases](../../releases) 下载
`TypeTide-Win-Setup-x.y.z.exe` 安装包或 `TypeTide-Win-Windows-x.y.z.zip` 便携包，也可自行编译。

1. 启动 TypeTide-Win，从托盘打开设置。
2. 添加 Providers、调整优先级，并使用 Test selected 测试。
3. 选择母语和外语，并验证快捷键。
4. 选中文字按 Alt+D 阅读，按 Alt+R 翻译并替换，或按 Alt+Q（也可从托盘打开）输入翻译。

设置和崩溃报告保存在 `%APPDATA%\TypeTide-Win\`。
本 fork 使用独立的安装标识、自启动项和凭据，不与上游 TypeTide 共用配置。
更新检查和下载地址均指向 **wadaxiyang/typetide-win**。

## 编译与测试

安装 Visual Studio 2022 或更新版本，勾选“使用 C++ 的桌面开发”，包含 Windows SDK、CMake 3.24+ 和 Ninja。
在项目根目录打开 PowerShell：

```powershell
.\windows\scripts\build.ps1
```

产物：`windows\build\TypeTide-Win.exe`。

也可在 Visual Studio Developer PowerShell 中运行：

```powershell
cmake -S windows -B windows/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build
```

使用隔离目录运行内置自检：

```powershell
$env:TYPETIDE_DATA_DIR = "$env:TEMP\typetide-win-selftest"
$p = Start-Process .\windows\build\TypeTide-Win.exe -ArgumentList '--selftest' -NoNewWindow -Wait -PassThru
if ($p.ExitCode -ne 0) { throw "Self-test failed: $($p.ExitCode)" }
Remove-Item Env:\TYPETIDE_DATA_DIR
```

在 Developer PowerShell 中运行弹窗及本地 HTTP 服务回归测试：

```powershell
ctest --test-dir windows/build --output-on-failure
```

这些回归测试不会访问外部模型服务。

开发细节见 [Windows 开发文档](windows/README.md)，完整人工验证项目见 [测试清单](windows/TESTING.md)。
保留 `contracts/translation-contracts.json`，用于语言方向、提示词与流式响应的回归验证。

## 打包与发布

```powershell
# 可选：递增并同步版本；默认 patch +1，或 -Minor / -Major / -Version 1.2.3
.\scripts\bump-version.ps1

# 编译、自检并生成安装包和便携 ZIP（需要 Inno Setup 6）
.\windows\scripts\build-release.ps1

# 提交发布代码后推送版本 tag，由 GitHub Actions 构建并发布到本 fork
.\scripts\release-windows.ps1
```

SVG 源文件及生成的原生资源已包含在仓库中，正常编译不需要 Python。
修改 SVG 后运行 `python scripts/generate-brand-assets.py` 更新矢量路径，再运行
`windows/scripts/make-icon.ps1` 导出透明 ICO。

## 来源与许可

- 本项目：[wadaxiyang/typetide-win](https://github.com/wadaxiyang/typetide-win)
- 原作者及上游参考：[everettjf/typetide](https://github.com/everettjf/typetide)
- [MIT License](LICENSE)，保留原作者版权声明。
