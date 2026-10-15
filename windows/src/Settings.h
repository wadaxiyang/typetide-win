//
//  Settings.h — 全局设置，
//  JSON 持久化到 %APPDATA%\TypeTide-Win\settings.json。
//
#pragma once
#include <windows.h>
#include "Language.h"
#include <string>
#include <vector>
#include <array>

enum class TranslationBackend { Ollama, OpenAI, Anthropic, Gemini };
struct ProviderConfig {
    std::string id;
    std::string name = "Ollama";
    TranslationBackend kind = TranslationBackend::Ollama;
    std::string baseURL = "http://127.0.0.1:11434";
    std::string model = "qwen2.5:3b";
    std::string apiKey;
};
const char* ProviderKindCode(TranslationBackend kind);
ProviderConfig NewProvider(TranslationBackend kind);
bool ProviderIsLocal(const ProviderConfig& provider);
enum class Appearance { System, Light, Dark };

/// 划词触发方式
enum class SelectionTrigger {
    None,  // 仅快捷键
    Icon,  // 划词后显示小图标，点击翻译
    Auto,  // 划词后直接弹出翻译
};

enum class ShortcutAction {
    SmartPopup,
    NativePopup,
    ForeignPopup,
    SmartReplace,
    NativeReplace,
    ForeignReplace,
};

/// 全局快捷键（RegisterHotKey 语义）
struct Hotkey {
    UINT modifiers = MOD_ALT;  // MOD_ALT / MOD_CONTROL / MOD_SHIFT / MOD_WIN 组合
    UINT vk = 'D';

    std::wstring displayString() const;
    bool isConfigured() const { return vk != 0; }
    bool operator==(const Hotkey&) const = default;
};

enum class Feature { Selection, Rewrite, Input };
// Negative values follow shared preferences; nonnegative values are Language.
constexpr int kLanguageAuto = -1;
constexpr int kLanguageNative = -2;
constexpr int kLanguageForeign = -3;
constexpr int kLanguageSame = -4;
struct FeatureConfig {
    int source = kLanguageAuto;
    int target = kLanguageAuto;
    RewriteStyle style = RewriteStyle::Faithful;
    std::string providerId; // empty = default ordered provider chain
    std::string prompt;     // empty = built-in template
    bool operator==(const FeatureConfig&) const = default;
};
const wchar_t* FeatureName(Feature feature);

struct Settings {
    bool enabled = true;
    Appearance appearance = Appearance::System;
    bool useDarkMode() const;
    TranslationBackend backend = TranslationBackend::Ollama;

    // Ollama
    std::string ollamaHost = "http://127.0.0.1";
    int ollamaPort = 11434;
    std::string ollamaModel = "qwen2.5:3b";

    // OpenAI 兼容
    std::string openAIBaseURL = "https://api.openai.com/v1";
    std::string openAIKey;
    std::string openAIModel = "gpt-5-mini";

    // Legacy fields above are read only for migration/old settings compatibility.
    std::vector<ProviderConfig> providers;
    bool providersConfigured = false;
    std::vector<ProviderConfig> effectiveProviders() const;
    void ensureProviders();

    // 语言（native = 母语，foreign = 外语）
    Language nativeLanguage = Language::Chinese;
    Language foreignLanguage = Language::English;
    TranslationDirection readDirection = TranslationDirection::Auto;
    TranslationDirection rewriteDirection = TranslationDirection::Auto;

    // 快捷键
    Hotkey readShortcut{MOD_ALT, 'D'};
    Hotkey rewriteShortcut{MOD_ALT, 'R'};
    Hotkey inputShortcut{MOD_ALT, 'Q'};
    std::array<FeatureConfig, 3> features{{{}, {}, {kLanguageNative, kLanguageForeign}}};
    std::string featureMigrationNotice;
    FeatureConfig& feature(Feature f) { return features[static_cast<size_t>(f)]; }
    const FeatureConfig& feature(Feature f) const { return features[static_cast<size_t>(f)]; }
    Hotkey& shortcut(Feature f);
    const Hotkey& shortcut(Feature f) const;
    std::vector<ProviderConfig> providersFor(const FeatureConfig& config) const;
    bool providerMissing(const FeatureConfig& config) const;
    std::wstring providerSummary(const FeatureConfig& config) const;
    std::wstring languageLabel(int value, bool source = false) const;
    ResolvedDirection resolveFeature(const std::wstring& text, Feature f,
                                           const FeatureConfig& config) const;
    void migrateFeatures();
    ShortcutAction readShortcutAction = ShortcutAction::SmartPopup;
    ShortcutAction rewriteShortcutAction = ShortcutAction::SmartReplace;

    // 行为
    SelectionTrigger selectionTrigger = SelectionTrigger::None;
    bool rewritePreview = false;  // false = 直接替换
    RewriteStyle readStyle = RewriteStyle::Faithful;
    RewriteStyle rewriteStyle = RewriteStyle::Faithful;

    // 跳过的应用（exe 名，不区分大小写，可不带 .exe）
    std::vector<std::wstring> skipApps;

    bool hasCompletedFirstLaunch = false;
    long long lastUpdateCheck = 0;  // Unix 秒

    // --- 持久化 ---
    static Settings& shared();
    void load();
    /// 原子保存；失败时保留旧文件并返回 false。
    bool save() const;
    static std::wstring filePath();
};
