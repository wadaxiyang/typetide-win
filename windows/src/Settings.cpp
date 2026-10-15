#include "Settings.h"
#include "Credentials.h"
#include "Json.h"
#include "Util.h"
#include <fstream>
#include <sstream>
#include <objbase.h>

const char* ProviderKindCode(TranslationBackend kind) {
    switch (kind) {
    case TranslationBackend::OpenAI: return "openai";
    case TranslationBackend::Anthropic: return "anthropic";
    case TranslationBackend::Gemini: return "gemini";
    default: return "ollama";
    }
}
ProviderConfig NewProvider(TranslationBackend kind) {
    ProviderConfig p;
    GUID guid{}; CoCreateGuid(&guid);
    wchar_t id[40]{}; StringFromGUID2(guid, id, 40);
    p.id = util::Narrow(id);
    p.kind = kind;
    if (kind != TranslationBackend::Ollama) p.model.clear();
    switch (kind) {
    case TranslationBackend::OpenAI: p.name = "OpenAI"; p.baseURL = "https://api.openai.com/v1"; break;
    case TranslationBackend::Anthropic: p.name = "Anthropic"; p.baseURL = "https://api.anthropic.com/v1"; break;
    case TranslationBackend::Gemini: p.name = "Gemini"; p.baseURL = "https://generativelanguage.googleapis.com/v1beta"; break;
    default: break;
    }
    return p;
}
std::vector<ProviderConfig> Settings::effectiveProviders() const {
    if (providersConfigured) return providers;
    ProviderConfig p;
    p.id = "legacy-primary";
    p.kind = backend;
    p.name = backend == TranslationBackend::Ollama ? "Ollama" : "OpenAI-compatible";
    p.baseURL = backend == TranslationBackend::Ollama ? ollamaHost + ":" + std::to_string(ollamaPort) : openAIBaseURL;
    p.model = backend == TranslationBackend::Ollama ? ollamaModel : openAIModel;
    p.apiKey = backend == TranslationBackend::Ollama ? "" : openAIKey;
    return {p};
}
void Settings::ensureProviders() {
    if (providersConfigured) return;
    providers = effectiveProviders();
    providersConfigured = true;
}

const wchar_t* FeatureName(Feature f) {
    switch (f) {
    case Feature::Selection: return L"Selection translation";
    case Feature::Rewrite: return L"Rewrite & replace";
    default: return L"Input translation";
    }
}
Hotkey& Settings::shortcut(Feature f) {
    return f == Feature::Selection ? readShortcut : f == Feature::Rewrite ? rewriteShortcut : inputShortcut;
}
const Hotkey& Settings::shortcut(Feature f) const {
    return f == Feature::Selection ? readShortcut : f == Feature::Rewrite ? rewriteShortcut : inputShortcut;
}
std::vector<ProviderConfig> Settings::providersFor(const FeatureConfig& config) const {
    auto list = effectiveProviders();
    if (!config.providerId.empty())
        for (const auto& p : list) if (p.id == config.providerId) return {p};
    return list;
}
bool Settings::providerMissing(const FeatureConfig& config) const {
    if (config.providerId.empty()) return false;
    for (const auto& p : effectiveProviders()) if (p.id == config.providerId) return false;
    return true;
}
bool ProviderIsLocal(const ProviderConfig& provider) {
    const auto url = util::ToLower(util::Widen(provider.baseURL));
    const auto scheme = url.find(L"://");
    if (scheme == std::wstring::npos || (url.substr(0, scheme) != L"http" && url.substr(0, scheme) != L"https")) return false;
    const auto authority = url.substr(scheme + 3, url.find_first_of(L"/?#", scheme + 3) - (scheme + 3));
    if (authority.empty() || authority.find(L'@') != std::wstring::npos) return false;
    const auto host = authority.front() == L'[' ? authority.substr(0, authority.find(L']') + 1)
                                             : authority.substr(0, authority.find(L':'));
    return host == L"localhost" || host == L"127.0.0.1" || host == L"[::1]";
}
std::wstring Settings::providerSummary(const FeatureConfig& config) const {
    std::wstring result = providerMissing(config) ? L"Provider removed; using default: "
        : config.providerId.empty() ? L"Default: " : L"";
    const auto list = providersFor(config);
    if (list.empty()) return result + L"No providers configured";
    for (size_t i = 0; i < list.size(); ++i) {
        if (i) result += L" → ";
        const auto& p = list[i];
        const bool local = ProviderIsLocal(p);
        result += util::Widen(p.name + " · " + p.model) + (local ? L" (local)" : L" (network)");
    }
    return result;
}
std::wstring Settings::languageLabel(int value, bool source) const {
    if (value == kLanguageAuto) return source ? L"Auto detect" : L"Auto · bidirectional";
    if (value == kLanguageNative) return L"Native · " + lang::ShortName(nativeLanguage);
    if (value == kLanguageForeign) return L"Foreign · " + lang::ShortName(foreignLanguage);
    if (value == kLanguageSame) return L"Keep original language";
    return lang::ShortName(static_cast<Language>(value));
}
ResolvedDirection Settings::resolveFeature(const std::wstring& text, Feature f, const FeatureConfig& c) const {
    auto fixed = [&](int value) { return value == kLanguageNative ? nativeLanguage :
        value == kLanguageForeign ? foreignLanguage : static_cast<Language>(value); };
    auto source = c.source == kLanguageAuto ? lang::Detect(text) : std::optional<Language>(fixed(c.source));
    Language target;
    if (c.target == kLanguageSame) target = source.value_or(nativeLanguage);
    else if (c.target != kLanguageAuto) target = fixed(c.target);
    else if (source == nativeLanguage) target = foreignLanguage;
    else if (source == foreignLanguage) target = nativeLanguage;
    else target = f == Feature::Selection ? nativeLanguage : foreignLanguage;
    return {source, target};
}
void Settings::migrateFeatures() {
    auto migrate = [&](Feature f, TranslationDirection direction, RewriteStyle style, ShortcutAction action) {
        auto& c = feature(f);
        c.style = style;
        if (direction == TranslationDirection::NativeToForeign) { c.source = kLanguageNative; c.target = kLanguageForeign; }
        if (direction == TranslationDirection::ForeignToNative) { c.source = kLanguageForeign; c.target = kLanguageNative; }
        if (action == ShortcutAction::NativePopup || action == ShortcutAction::NativeReplace) { c.source = kLanguageAuto; c.target = kLanguageNative; }
        if (action == ShortcutAction::ForeignPopup || action == ShortcutAction::ForeignReplace) { c.source = kLanguageAuto; c.target = kLanguageForeign; }
    };
    const bool readReplaces = static_cast<int>(readShortcutAction) >= 3;
    const bool rewriteReplaces = static_cast<int>(rewriteShortcutAction) >= 3;
    if (readReplaces && !rewriteReplaces) {
        std::swap(readShortcut, rewriteShortcut);
        std::swap(readShortcutAction, rewriteShortcutAction);
        featureMigrationNotice = "Your shortcuts were moved to the features they previously performed.";
    } else if (readReplaces || !rewriteReplaces) {
        // A feature now has one shortcut. Preserve the binding already belonging
        // to that feature and retain the other in its original slot; explain it.
        featureMigrationNotice = "Legacy shortcuts both targeted one action. Keys were retained, but now each key runs its named feature. Review them below.";
    }
    migrate(Feature::Selection, readDirection, readStyle, readShortcutAction);
    migrate(Feature::Rewrite, rewriteDirection, rewriteStyle, rewriteShortcutAction);
    if (inputShortcut.isConfigured() && (inputShortcut == readShortcut || inputShortcut == rewriteShortcut)) {
        inputShortcut = {0, 0};
        if (!featureMigrationNotice.empty()) featureMigrationNotice += " ";
        featureMigrationNotice += "Alt+Q is already used by an existing feature. Input translation is available from the tray; choose its shortcut in Features.";
    }
}

std::wstring Hotkey::displayString() const {
    if (!isConfigured()) return L"Not set";
    std::wstring s;
    if (modifiers & MOD_CONTROL) s += L"Ctrl+";
    if (modifiers & MOD_SHIFT) s += L"Shift+";
    if (modifiers & MOD_ALT) s += L"Alt+";
    if (modifiers & MOD_WIN) s += L"Win+";

    wchar_t name[64]{};
    UINT scan = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LONG l = (LONG)(scan << 16);
    switch (vk) {  // 扩展键需要置扩展位才能取到正确名字
    case VK_LEFT: case VK_RIGHT: case VK_UP: case VK_DOWN:
    case VK_HOME: case VK_END: case VK_PRIOR: case VK_NEXT:
    case VK_INSERT: case VK_DELETE:
        l |= 1 << 24; break;
    }
    if (GetKeyNameTextW(l, name, 64) > 0) s += name;
    else s += (wchar_t)vk;
    return s;
}

Settings& Settings::shared() {
    static Settings s = [] {
        Settings v;
        v.load();
        return v;
    }();
    return s;
}

std::wstring Settings::filePath() {
    return util::AppDataDir() + L"\\settings.json";
}

bool Settings::useDarkMode() const {
    if (appearance != Appearance::System) return appearance == Appearance::Dark;
    DWORD light = 1, size = sizeof(light);
    RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

void Settings::load() {
    std::ifstream f(filePath(), std::ios::binary);
    if (!f) return;
    f.seekg(0, std::ios::end);
    const auto length = f.tellg();
    if (length < 0 || length > 1024 * 1024) {
        util::Log("settings: refusing invalid file size");
        return;
    }
    f.seekg(0, std::ios::beg);
    std::stringstream ss;
    ss << f.rdbuf();
    bool ok = false;
    json::Value root = json::Parse(ss.str(), &ok);
    if (!ok || !root.isObject()) {
        util::Log("settings: invalid JSON, using safe defaults");
        return;
    }

    auto str = [&](const char* k, const std::string& def) {
        return root[k].isString() ? root[k].asString() : def;
    };
    auto boolean = [&](const char* k, bool def) {
        return root[k].type() == json::Value::Type::Bool ? root[k].asBool() : def;
    };

    enabled = boolean("enabled", enabled);
    const std::string theme = str("appearance", "system");
    appearance = theme == "dark" ? Appearance::Dark : theme == "light" ? Appearance::Light : Appearance::System;
    backend = str("backend", "ollama") == "openai" ? TranslationBackend::OpenAI : TranslationBackend::Ollama;

    ollamaHost = str("ollamaHost", ollamaHost);
    ollamaPort = root["ollamaPort"].isNull() ? ollamaPort : root["ollamaPort"].asInt(ollamaPort);
    if (ollamaPort < 1 || ollamaPort > 65535) ollamaPort = 11434;
    ollamaModel = str("ollamaModel", ollamaModel);
    if (ollamaHost.size() > 2048) ollamaHost = "http://127.0.0.1";
    if (ollamaModel.size() > 256) ollamaModel = "qwen2.5:3b";

    openAIBaseURL = str("openAIBaseURL", openAIBaseURL);
    if (openAIBaseURL.size() > 2048) openAIBaseURL = "https://api.openai.com/v1";
    const std::string legacyKey = str("openAIKey", "");
    wchar_t testDir[2]{};
    const bool isolatedTest = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir, 2) != 0;
    bool migratedLegacyKey = false;
    if (isolatedTest) {
        openAIKey = legacyKey;
    } else {
        openAIKey = credentials::LoadCloudApiKey();
        if (openAIKey.empty() && !legacyKey.empty()) {
            openAIKey = legacyKey;
            credentials::SaveCloudApiKey(openAIKey); // migrate old plaintext settings
            migratedLegacyKey = true;
        }
    }
    openAIModel = str("openAIModel", openAIModel);
    if (openAIModel.size() > 256) openAIModel = "gpt-5-mini";

    providersConfigured = root["providers"].isArray();
    providers.clear();
    if (const auto* entries = root["providers"].array()) {
        for (const auto& entry : *entries) {
            if (!entry.isObject() || providers.size() >= 32) continue;
            const auto kind = entry["kind"].asString();
            if (kind != "ollama" && kind != "openai" && kind != "anthropic" && kind != "gemini") continue;
            ProviderConfig p;
            p.kind = kind == "openai" ? TranslationBackend::OpenAI : kind == "anthropic" ? TranslationBackend::Anthropic : kind == "gemini" ? TranslationBackend::Gemini : TranslationBackend::Ollama;
            p.id = entry["id"].asString();
            bool duplicate = false;
            for (const auto& existing : providers) if (existing.id == p.id) duplicate = true;
            if (p.id.empty() || p.id.size() > 80 || duplicate) continue;
            p.name = entry["name"].asString().substr(0, 100);
            p.baseURL = entry["baseURL"].asString().substr(0, 2048);
            p.model = entry["model"].asString().substr(0, 256);
            p.apiKey = isolatedTest ? entry["apiKey"].asString() : credentials::LoadProviderKey(p.id);
            providers.push_back(std::move(p));
        }
    }

    if (auto l = lang::FromCode(str("nativeLanguage", ""))) nativeLanguage = *l;
    if (auto l = lang::FromCode(str("foreignLanguage", ""))) foreignLanguage = *l;
    if (auto d = lang::DirectionFromCode(str("readDirection", ""))) readDirection = *d;
    if (auto d = lang::DirectionFromCode(str("rewriteDirection", ""))) rewriteDirection = *d;

    auto loadHotkey = [&](const char* k, Hotkey& hk) {
        const json::Value& v = root[k];
        if (v.isObject()) {
            UINT modifiers = (UINT)v["modifiers"].asInt((int)hk.modifiers);
            UINT vk = (UINT)v["vk"].asInt((int)hk.vk);
            constexpr UINT allowed = MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
            if ((modifiers & ~allowed) == 0 && vk < 256) {
                hk.modifiers = modifiers;
                hk.vk = vk;
            }
        }
    };
    loadHotkey("readShortcut", readShortcut);
    loadHotkey("rewriteShortcut", rewriteShortcut);
    loadHotkey("inputShortcut", inputShortcut);
    auto loadAction = [&](const char* key, ShortcutAction fallback) {
        const std::string value = str(key, "");
        if (value == "nativePopup") return ShortcutAction::NativePopup;
        if (value == "foreignPopup") return ShortcutAction::ForeignPopup;
        if (value == "smartReplace") return ShortcutAction::SmartReplace;
        if (value == "nativeReplace") return ShortcutAction::NativeReplace;
        if (value == "foreignReplace") return ShortcutAction::ForeignReplace;
        if (value == "smartPopup") return ShortcutAction::SmartPopup;
        return fallback;
    };
    readShortcutAction = loadAction("readShortcutAction", ShortcutAction::SmartPopup);
    rewriteShortcutAction = loadAction("rewriteShortcutAction", ShortcutAction::SmartReplace);

    std::string trig = str("selectionTrigger", "none");
    selectionTrigger = trig == "icon" ? SelectionTrigger::Icon
                     : trig == "auto" ? SelectionTrigger::Auto
                     : SelectionTrigger::None;
    rewritePreview = boolean("rewritePreview", rewritePreview);
    if (auto s2 = lang::StyleFromCode(str("readStyle", ""))) readStyle = *s2;
    if (auto s2 = lang::StyleFromCode(str("rewriteStyle", ""))) rewriteStyle = *s2;

    features = Settings{}.features;
    featureMigrationNotice.clear();
    if (!root["features"].isObject()) migrateFeatures();
    else {
        const char* names[] = {"selection", "rewrite", "input"};
        for (size_t i = 0; i < features.size(); ++i) {
            auto& c = features[i];
            const auto& v = root["features"][names[i]];
            auto validLanguage = [](int value, bool source) {
                return (value >= 0 && value < (int)std::size(lang::All)) ||
                    value == kLanguageAuto || value == kLanguageNative || value == kLanguageForeign ||
                    (!source && value == kLanguageSame);
            };
            int source = v["source"].asInt(c.source), target = v["target"].asInt(c.target);
            if (validLanguage(source, true)) c.source = source;
            if (validLanguage(target, false)) c.target = target;
            if (auto style = lang::StyleFromCode(v["style"].asString())) c.style = *style;
            c.providerId = v["providerId"].asString().substr(0, 80);
            c.prompt = v["prompt"].asString().substr(0, 32000);
        }
        featureMigrationNotice = str("featureMigrationNotice", "");
    }

    skipApps.clear();
    if (const json::Array* arr = root["skipApps"].array())
        for (auto& item : *arr) {
            if (!item.isString() || skipApps.size() >= 100) continue;
            std::wstring app = util::Trim(util::Widen(item.asString()));
            if (app.empty() || app.size() > 260) continue;
            bool duplicate = false;
            for (const auto& existing : skipApps)
                if (util::ToLower(existing) == util::ToLower(app)) duplicate = true;
            if (!duplicate) skipApps.push_back(std::move(app));
        }

    hasCompletedFirstLaunch = boolean("hasCompletedFirstLaunch", false);
    lastUpdateCheck = (long long)root["lastUpdateCheck"].asNumber(0);
    if (migratedLegacyKey) save(); // rewrite JSON without the plaintext key
}

bool Settings::save() const {
    json::Object root;
    root["enabled"] = enabled;
    root["appearance"] = appearance == Appearance::Dark ? "dark" : appearance == Appearance::Light ? "light" : "system";
    root["backend"] = backend == TranslationBackend::OpenAI ? "openai" : "ollama";

    root["ollamaHost"] = ollamaHost;
    root["ollamaPort"] = ollamaPort;
    root["ollamaModel"] = ollamaModel;

    root["openAIBaseURL"] = openAIBaseURL;
    wchar_t testDir[2]{};
    const bool isolatedTest = GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", testDir, 2) != 0;
    if (isolatedTest) root["openAIKey"] = openAIKey;
    else if (!credentials::SaveCloudApiKey(openAIKey)) {
        util::Log("settings: credential save failed (%lu)", GetLastError());
        return false;
    }
    root["openAIModel"] = openAIModel;

    if (providersConfigured) {
        json::Array entries;
        for (const auto& p : providers) {
            json::Object item;
            item["id"] = p.id; item["name"] = p.name;
            item["kind"] = ProviderKindCode(p.kind);
            item["baseURL"] = p.baseURL; item["model"] = p.model;
            if (isolatedTest) item["apiKey"] = p.apiKey;
            else if (!credentials::SaveProviderKey(p.id, p.apiKey)) return false;
            entries.emplace_back(std::move(item));
        }
        root["providers"] = std::move(entries);
    }

    root["nativeLanguage"] = lang::Code(nativeLanguage);
    root["foreignLanguage"] = lang::Code(foreignLanguage);
    root["readDirection"] = lang::DirectionCode(readDirection);
    root["rewriteDirection"] = lang::DirectionCode(rewriteDirection);

    auto hotkeyJson = [](const Hotkey& hk) {
        json::Object o;
        o["modifiers"] = (int)hk.modifiers;
        o["vk"] = (int)hk.vk;
        return json::Value(std::move(o));
    };
    root["readShortcut"] = hotkeyJson(readShortcut);
    root["rewriteShortcut"] = hotkeyJson(rewriteShortcut);
    root["inputShortcut"] = hotkeyJson(inputShortcut);
    json::Object featureJson;
    const char* featureNames[] = {"selection", "rewrite", "input"};
    for (size_t i = 0; i < features.size(); ++i) {
        const auto& c = features[i];
        featureJson[featureNames[i]] = json::Object{{"source", c.source}, {"target", c.target},
            {"style", lang::StyleCode(c.style)}, {"providerId", c.providerId}, {"prompt", c.prompt}};
    }
    root["features"] = std::move(featureJson);
    root["featureMigrationNotice"] = featureMigrationNotice;
    auto actionCode = [](ShortcutAction action) -> std::string {
        switch (action) {
        case ShortcutAction::NativePopup: return "nativePopup";
        case ShortcutAction::ForeignPopup: return "foreignPopup";
        case ShortcutAction::SmartReplace: return "smartReplace";
        case ShortcutAction::NativeReplace: return "nativeReplace";
        case ShortcutAction::ForeignReplace: return "foreignReplace";
        case ShortcutAction::SmartPopup:
        default: return "smartPopup";
        }
    };
    root["readShortcutAction"] = actionCode(readShortcutAction);
    root["rewriteShortcutAction"] = actionCode(rewriteShortcutAction);

    root["selectionTrigger"] = selectionTrigger == SelectionTrigger::Icon ? "icon"
                             : selectionTrigger == SelectionTrigger::Auto ? "auto"
                             : "none";
    root["rewritePreview"] = rewritePreview;
    root["readStyle"] = lang::StyleCode(readStyle);
    root["rewriteStyle"] = lang::StyleCode(rewriteStyle);

    json::Array apps;
    for (auto& a : skipApps) apps.push_back(util::Narrow(a));
    root["skipApps"] = json::Value(std::move(apps));

    root["hasCompletedFirstLaunch"] = hasCompletedFirstLaunch;
    root["lastUpdateCheck"] = (double)lastUpdateCheck;

    // 原子写：先写临时文件再 rename 替换，进程中途被杀也不会留下半截 JSON
    std::wstring path = filePath();
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            util::Log("settings: cannot open temp file (%lu)", GetLastError());
            return false;
        }
        f << json::Value(std::move(root)).dump();
        f.flush();
        if (!f) {
            util::Log("settings: temp write failed");
            f.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    }
    // Saving may run on the UI thread. Never sleep/retry inside the message loop.
    if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    util::Log("settings: atomic replace failed (%lu)", GetLastError());
    DeleteFileW(tmp.c_str());
    return false;
}
