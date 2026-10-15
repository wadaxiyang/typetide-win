#include <winsock2.h>
#include <ws2tcpip.h>
#include "ProviderProtocol.h"
#include "Json.h"
#include "Util.h"
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <thread>
#include <atomic>
#include <memory>
#include <algorithm>

namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
// A loopback HTTP fixture exercises production WinHTTP, request serialization,
// stream parsing, priority routing and cancellation without contacting a model.
class Server {
public:
    std::string base;
    Server() {
        WSADATA data{}; WSAStartup(MAKEWORD(2, 2), &data);
        listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        sockaddr_in address{}; address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        bind(listener, (sockaddr*)&address, sizeof(address));
        int length = sizeof(address); getsockname(listener, (sockaddr*)&address, &length);
        base = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        listen(listener, 8);
        worker = std::thread([this] { serve(); });
    }
    ~Server() { closesocket(listener); worker.join(); WSACleanup(); }
    std::vector<std::string> requests() { std::lock_guard lock(mutex); return seen; }
private:
    SOCKET listener;
    std::thread worker;
    std::mutex mutex;
    std::vector<std::string> seen;
    void serve() {
        for (;;) {
            const auto client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) return;
            std::string request;
            char buf[4096];
            size_t headerEnd = std::string::npos, expected = 0;
            while (true) {
                const int n = recv(client, buf, sizeof(buf), 0);
                if (n <= 0) break;
                request.append(buf, n);
                headerEnd = request.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    const auto at = request.find("Content-Length:");
                    expected = at == std::string::npos ? 0 : std::stoul(request.substr(at + 15));
                    if (request.size() >= headerEnd + 4 + expected) break;
                }
            }
            bool firstRecovery = false;
            { std::lock_guard lock(mutex);
                firstRecovery = request.find("/recover/") != std::string::npos &&
                    std::none_of(seen.begin(), seen.end(), [](const auto& old) { return old.find("/recover/") != std::string::npos; });
                seen.push_back(request);
            }
            const bool rejectThink = request.find("/reject-think/") != std::string::npos && request.find("\"think\":false") != std::string::npos;
            const bool fail = request.find("/http500/") != std::string::npos || firstRecovery || rejectThink;
            const bool partial = request.find("/partial/") != std::string::npos;
            const bool slow = request.find("/slow/") != std::string::npos;
            std::string payload;
            if (fail) payload = R"({"error":{"message":"Synthetic failure"}})";
            else if (partial) payload = "data: {\"choices\":[{\"delta\":{\"content\":\"discard me\"}}]}\n\n";
            else if (request.find("/messages ") != std::string::npos)
                payload = "data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"text_delta\",\"text\":\"Anthropic success\"}}\n\ndata: {\"type\":\"message_stop\"}\n\n";
            else if (request.find(":streamGenerateContent?alt=sse ") != std::string::npos)
                payload = "data: {\"candidates\":[{\"content\":{\"parts\":[{\"text\":\"Gemini success\"}]},\"finishReason\":\"STOP\"}]}\n\n";
            else if (request.find("/api/generate ") != std::string::npos)
                payload = "{\"response\":\"Ollama success\",\"done\":true}\n";
            else payload = "data: {\"choices\":[{\"delta\":{\"content\":\"OpenAI success\"},\"finish_reason\":\"stop\"}]}\n\ndata: [DONE]\n\n";
            if (slow) std::this_thread::sleep_for(std::chrono::milliseconds(300));
            const auto response = std::string(rejectThink ? "HTTP/1.1 400 Bad Request\r\n" : fail ? "HTTP/1.1 503 Service Unavailable\r\n" : "HTTP/1.1 200 OK\r\n") +
                "Content-Type: text/event-stream\r\nConnection: close\r\nContent-Length: " + std::to_string(payload.size()) + "\r\n\r\n" + payload;
            // Deliberately fragment both headers and JSON; WinHTTP may coalesce.
            for (size_t offset = 0; offset < response.size();) {
                const int n = send(client, response.data() + offset, (int)std::min<size_t>(17, response.size() - offset), 0);
                if (n <= 0) break;
                offset += n;
            }
            closesocket(client);
        }
    }
};
ProviderConfig config(const Server& server, TranslationBackend kind, const char* route) {
    auto p = NewProvider(kind); p.name = route; p.baseURL = server.base + "/" + route;
    p.model = "fixture-model"; p.apiKey = "synthetic-test-key"; return p;
}
struct Result { bool called = false, ok = false; std::wstring text, error; int deltas = 0; };
Result translate(const std::vector<ProviderConfig>& providers, bool cache = false,
                 TranslationRequest request = {L"synthetic input"}) {
    struct State { std::mutex mutex; std::condition_variable cv; Result result; };
    auto state = std::make_shared<State>();
    const auto id = translator::StreamWithProviders(request, providers,
        [state](uint64_t, const std::wstring& delta) { std::lock_guard lock(state->mutex); state->result.text += delta; ++state->result.deltas; },
        [state](uint64_t, bool ok, const std::wstring& error) { std::lock_guard lock(state->mutex);
            state->result.called = true; state->result.ok = ok; state->result.error = error; state->cv.notify_all(); }, cache);
    std::unique_lock lock(state->mutex);
    if (!state->cv.wait_for(lock, std::chrono::seconds(5), [&] { return state->result.called; })) translator::Cancel(id);
    return state->result;
}
}
int main() {
    wchar_t isolated[2]{};
    if (!GetEnvironmentVariableW(L"TYPETIDE_DATA_DIR", isolated, 2)) {
        std::fprintf(stderr, "Run through CTest or set TYPETIDE_DATA_DIR to an isolated test directory.\n");
        return 2;
    }
    Server server;
    auto a = config(server, TranslationBackend::OpenAI, "http500");
    auto b = config(server, TranslationBackend::OpenAI, "partial");
    auto c = config(server, TranslationBackend::OpenAI, "openai");
    auto d = config(server, TranslationBackend::Anthropic, "anthropic");
    auto e = config(server, TranslationBackend::Gemini, "gemini");
    auto f = config(server, TranslationBackend::Ollama, "ollama");
    auto r = translate({a, b, c, d});
    auto requests = server.requests();
    check(r.called && r.ok && r.text == L"OpenAI success" && r.deltas == 1, "HTTP error and unfinished stream fall back without leaking partial output");
    check(requests.size() == 3 && requests[0].find("/http500/") != std::string::npos && requests[1].find("/partial/") != std::string::npos && requests[2].find("/openai/") != std::string::npos, "tries providers in order and stops after success");
    check(translate({d}).text == L"Anthropic success", "native Anthropic response translates");
    check(translate({e}).text == L"Gemini success", "native Gemini response translates (SSE query preserved)");
    check(translate({f}).text == L"Ollama success", "native Ollama response translates");
    requests = server.requests();
    check(requests[3].find("anthropic-version: 2023-06-01") != std::string::npos && requests[3].find("x-api-key: synthetic-test-key") != std::string::npos, "Anthropic headers are sent");
    check(requests[4].find("x-goog-api-key: synthetic-test-key") != std::string::npos, "Gemini key stays in header");
    bool valid = false;
    auto body = json::Parse(requests[3].substr(requests[3].find("\r\n\r\n") + 4), &valid);
    check(valid && body["max_tokens"].asInt() == 8192 && body["system"].isString() && body["messages"].size() == 1, "Anthropic request uses Messages schema");
    body = json::Parse(requests[4].substr(requests[4].find("\r\n\r\n") + 4), &valid);
    check(valid && body["systemInstruction"]["parts"].size() == 1 && body["contents"].size() == 1, "Gemini request uses native content schema");
    r = translate({a, b});
    check(!r.ok && r.text.empty() && r.error.find(L"1. http500") != std::wstring::npos && r.error.find(L"2. partial") != std::wstring::npos, "all failures report each attempted provider");
    check(!translate({}).ok, "empty provider list gives a setup error");
    translator::ClearCacheForTesting();
    translate({c, d}, true);
    check(translate({d, c}, true).text == L"Anthropic success", "reordering providers cannot return an old-priority cached result");
    auto changed = c; changed.baseURL = d.baseURL; changed.kind = d.kind;
    check(translate({changed}, true).text == L"Anthropic success", "endpoint and protocol are part of cache identity");
    auto recovering = c; recovering.baseURL = server.base + "/recover";
    check(translate({recovering, d}, true).text == L"Anthropic success" &&
          translate({recovering, d}, true).text == L"OpenAI success", "a recovered primary is retried after a cached fallback attempt");
    auto legacyOllama = f; legacyOllama.baseURL = server.base + "/reject-think";
    check(translate({legacyOllama}).text == L"Ollama success", "Ollama retries without think on HTTP 400");
    auto calls = std::make_shared<std::atomic<int>>(0);
    auto slow = c; slow.baseURL = server.base + "/slow";
    const size_t beforeCancel = server.requests().size();
    auto id = translator::StreamWithProviders({L"cancellation test"}, {slow, d},
        [calls](uint64_t, const std::wstring&) { ++*calls; },
        [calls](uint64_t, bool, const std::wstring&) { ++*calls; }, false);
    for (int i = 0; i < 100 && server.requests().size() == beforeCancel; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    translator::Cancel(id);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    check(*calls == 0 && server.requests().size() == beforeCancel + 1, "cancellation suppresses completion and does not try the next provider");
    std::wstring error;
    auto unsafe = c; unsafe.baseURL = "http://localhost.evil.example/v1";
    check(!provider::Validate(unsafe, error), "loopback lookalike cannot bypass HTTPS credential requirement");
    provider::Response truncated(TranslationBackend::OpenAI);
    truncated.line("data: {\"choices\":[{\"finish_reason\":\"length\",\"delta\":{\"content\":\"half\"}}]}");
    truncated.line("data: [DONE]");
    check(!truncated.error.empty(), "token-limit truncation is a failure even with a terminal event");
    provider::Response blocked(TranslationBackend::Gemini);
    blocked.line("data: {\"promptFeedback\":{\"blockReason\":\"SAFETY\"}}");
    check(!blocked.error.empty(), "Gemini blocked responses fail instead of returning partial output");
    provider::Response thinking(TranslationBackend::Anthropic);
    thinking.line("data: {\"type\":\"content_block_delta\",\"delta\":{\"type\":\"thinking_delta\",\"thinking\":\"hidden\"}}");
    check(thinking.text.empty(), "Anthropic thinking is excluded");
    const auto bomLines = translator::AssembleLinesForTesting({"\xEF", "\xBB", "\xBF", "data: [DONE]\r", "\n"});
    check(bomLines.size() == 1 && bomLines[0] == "data: [DONE]", "fragmented UTF-8 BOM and CRLF are assembled correctly");
    Settings saved; saved.providersConfigured = true; saved.providers = {d, c, e, f};
    check(saved.save(), "multi-provider settings save");
    Settings loaded; loaded.load();
    check(loaded.providers.size() == 4 && loaded.providers[0].id == d.id && loaded.providers[1].id == c.id && loaded.providers[2].kind == TranslationBackend::Gemini && loaded.providers[3].apiKey == f.apiKey, "provider order, stable IDs, formats and keys survive restart");
    saved.providers.clear(); saved.save(); loaded.load();
    check(loaded.providersConfigured && loaded.effectiveProviders().empty(), "intentionally empty list does not resurrect a legacy provider");
    std::ofstream legacy(Settings::filePath()); legacy << R"({"backend":"openai","openAIBaseURL":"https://example.test/v1","openAIModel":"old-model","openAIKey":"legacy-fixture"})"; legacy.close();
    loaded.load(); loaded.ensureProviders();
    check(loaded.providers.size() == 1 && loaded.providers[0].kind == TranslationBackend::OpenAI && loaded.providers[0].model == "old-model" && loaded.providers[0].apiKey == "legacy-fixture", "legacy single-provider settings migrate without losing configuration");
    auto locality = c;
    locality.baseURL = "https://localhost/v1";
    check(ProviderIsLocal(locality), "local provider disclosure recognizes HTTPS and path without explicit port");
    locality.baseURL = "http://[::1]:11434";
    check(ProviderIsLocal(locality), "local provider disclosure recognizes IPv6 loopback");
    locality.baseURL = "http://localhost.example.test/v1";
    check(!ProviderIsLocal(locality), "local-looking remote hostname is disclosed as network");
    Settings features;
    auto route = features.resolveFeature(L"你好", Feature::Input, features.feature(Feature::Input));
    check(route.source == Language::Chinese && route.target == Language::English,
          "input feature defaults to native-to-foreign");
    features.foreignLanguage = Language::French;
    route = features.resolveFeature(L"你好", Feature::Input, features.feature(Feature::Input));
    check(route.target == Language::French, "input follows shared foreign-language preference");
    auto& rewrite = features.feature(Feature::Rewrite);
    rewrite.target = kLanguageSame; rewrite.style = RewriteStyle::Polished;
    route = features.resolveFeature(L"This is an English sentence with the original language.", Feature::Rewrite, rewrite);
    check(route.source == Language::English && route.target == Language::English,
          "keep-original rewrites English without translating it");
    TranslationRequest same{L"12345", std::nullopt, Language::Chinese, RewriteStyle::Polished, "", true};
    check(translator::RenderPrompt(same).find("same language as the original text") != std::string::npos &&
          translator::RenderPrompt(same).find("clarity and flow") != std::string::npos,
          "undetectable source still instructs provider to preserve original language");
    TranslationRequest custom{L"User input {target} stays literal", Language::Chinese, Language::English,
        RewriteStyle::Formal, "Custom: {source} -> {target}.{style}"};
    check(translator::RenderPrompt(custom) == "Custom: Chinese -> English. Use a professional, formal tone.",
          "custom prompt renders source, target and style placeholders");
    for (const auto& provider : {c, d, e, f}) {
        provider::Request wire;
        std::wstring wireError;
        check(provider::BuildRequest(provider, custom, wire, wireError) &&
              wire.body.find("Custom: Chinese -> English.") != std::string::npos &&
              wire.body.find("User input {target} stays literal") != std::string::npos,
              "all four provider protocols receive custom prompt and unchanged input");
    }
    TranslationRequest builtin{L"test", Language::Chinese, Language::English, RewriteStyle::Formal};
    check(translator::RenderPrompt(builtin) == translator::SystemPrompt(builtin.target, builtin.source, builtin.style),
          "built-in template preserves existing system prompt");
    translator::ClearCacheForTesting();
    const auto beforePrompts = server.requests().size();
    check(translate({c}, true, custom).ok && translate({c}, true, custom).ok,
          "custom prompt translations can use cache");
    custom.prompt += " Extra instruction.";
    check(translate({c}, true, custom).ok && server.requests().size() == beforePrompts + 2,
          "changing the prompt invalidates cached translation");
    custom.preserveLanguage = true;
    check(translate({c}, true, custom).ok && server.requests().size() == beforePrompts + 3,
          "keep-original and translation cannot share cached output");
    features.providersConfigured = true; features.providers = {c, d};
    rewrite.providerId = d.id;
    check(features.providersFor(rewrite).size() == 1 && features.providersFor(rewrite)[0].id == d.id,
          "explicit feature provider is isolated from the default fallback chain");
    features.providers = {c};
    check(features.providerMissing(rewrite) && features.providersFor(rewrite)[0].id == c.id &&
          features.providerSummary(rewrite).find(L"removed") != std::wstring::npos,
          "removed provider falls back to default with visible disclosure");
    features.feature(Feature::Input).prompt = "Custom prompt {target}";
    features.inputShortcut = {MOD_CONTROL | MOD_ALT, 'T'};
    check(features.save(), "feature settings save");
    Settings restored; restored.load();
    check(restored.features == features.features && restored.inputShortcut == features.inputShortcut,
          "all feature settings and input shortcut survive restart");
    Settings migration;
    migration.readShortcutAction = ShortcutAction::ForeignReplace;
    migration.rewriteShortcutAction = ShortcutAction::NativePopup;
    migration.migrateFeatures();
    check(migration.readShortcut.vk == 'R' && migration.rewriteShortcut.vk == 'D' &&
          migration.feature(Feature::Selection).target == kLanguageNative &&
          migration.feature(Feature::Rewrite).target == kLanguageForeign,
          "cross-bound legacy shortcuts move to the actions they previously performed");
    Settings existingAltQ;
    existingAltQ.readShortcut = {MOD_ALT, 'Q'};
    existingAltQ.migrateFeatures();
    check(existingAltQ.readShortcut.vk == 'Q' && !existingAltQ.inputShortcut.isConfigured() &&
          existingAltQ.featureMigrationNotice.find("already used") != std::string::npos,
          "new default Alt+Q cannot steal an existing custom shortcut during migration");
    Settings duplicateAction;
    duplicateAction.readShortcutAction = ShortcutAction::SmartReplace;
    duplicateAction.migrateFeatures();
    check(!duplicateAction.featureMigrationNotice.empty() && duplicateAction.readShortcut.vk == 'D',
          "ambiguous legacy bindings retain keys and explain their normalized actions");
    std::ofstream oldFeatures(Settings::filePath());
    oldFeatures << R"({"readShortcut":{"modifiers":2,"vk":71},"rewriteDirection":"foreignToNative","readStyle":"formal","rewriteStyle":"polished"})";
    oldFeatures.close();
    Settings oldLoaded; oldLoaded.load();
    check(oldLoaded.readShortcut.vk == 'G' && oldLoaded.feature(Feature::Selection).style == RewriteStyle::Formal &&
          oldLoaded.feature(Feature::Rewrite).source == kLanguageForeign && oldLoaded.feature(Feature::Rewrite).target == kLanguageNative &&
          oldLoaded.feature(Feature::Rewrite).style == RewriteStyle::Polished && oldLoaded.inputShortcut.vk == 'Q',
          "legacy JSON migrates shortcuts, direction and styles; adds Alt+Q");
    check(oldLoaded.save(), "migrated features persist");
    Settings again; again.load();
    check(again.features == oldLoaded.features && again.readShortcut == oldLoaded.readShortcut,
          "feature migration is idempotent after saving");
    return failures ? 1 : 0;
}
