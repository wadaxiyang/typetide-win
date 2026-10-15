#include "Translator.h"
#include "Http.h"
#include "Json.h"
#include "LocalDiagnostics.h"
#include "Settings.h"
#include "ProviderProtocol.h"
#include "Util.h"
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace translator {

namespace {

// ---------- 提示词 ----------

std::string styleSuffix(RewriteStyle style) {
    const char* ins = lang::StyleInstruction(style);
    return *ins ? std::string(" ") + ins : std::string();
}

// ---------- LRU 缓存（key = backend|from|to|style|text）----------

class Cache {
public:
    static Cache& shared() {
        static Cache c;
        return c;
    }

    std::optional<std::wstring> get(const std::string& key) {
        std::lock_guard lock(mu_);
        auto it = map_.find(key);
        if (it == map_.end()) return std::nullopt;
        order_.splice(order_.end(), order_, it->second.orderIt);
        return it->second.value;
    }

    void set(const std::string& key, const std::wstring& value) {
        std::lock_guard lock(mu_);
        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second.value = value;
            order_.splice(order_.end(), order_, it->second.orderIt);
            return;
        }
        order_.push_back(key);
        map_[key] = {value, std::prev(order_.end())};
        while (map_.size() > kCapacity) {
            map_.erase(order_.front());
            order_.pop_front();
        }
    }

    void clear() {
        std::lock_guard lock(mu_);
        map_.clear();
        order_.clear();
    }

private:
    static constexpr size_t kCapacity = 200;
    struct Entry {
        std::wstring value;
        std::list<std::string>::iterator orderIt;
    };
    std::mutex mu_;
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> order_;
};

std::string cacheKey(const TranslationRequest& req, const char* backend) {
    std::string k = backend;
    k += '|';
    k += req.source ? lang::Code(*req.source) : "auto";
    k += '|';
    k += lang::Code(req.target);
    k += '|';
    k += lang::StyleCode(req.style);
    k += '|';
    k += std::to_string(req.prompt.size()) + ":" + req.prompt;
    k += req.preserveLanguage ? "|same|" : "|translate|";
    k += util::Narrow(req.text);
    return k;
}

// ---------- 流式行组装（NDJSON / SSE 都按行切）----------

class LineAssembler {
public:
    /// 喂入一块字节，产出完整行（不含换行符）
    void feed(const char* data, size_t len, const std::function<void(const std::string&)>& onLine) {
        buf_.append(data, len);
        // A BOM may itself be split across network chunks.
        if (!bomChecked_) {
            if (buf_.size() < 3) return;
            if (buf_.compare(0, 3, "\xEF\xBB\xBF") == 0) buf_.erase(0, 3);
            bomChecked_ = true;
        }
        size_t pos;
        while ((pos = buf_.find('\n')) != std::string::npos) {
            std::string line = buf_.substr(0, pos);
            buf_.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.empty()) onLine(line);
        }
    }

    void finish(const std::function<void(const std::string&)>& onLine) {
        if (!buf_.empty()) {
            if (buf_.back() == '\r') buf_.pop_back();
            if (!buf_.empty()) onLine(buf_);
            buf_.clear();
        }
    }

private:
    std::string buf_;
    bool bomChecked_ = false;
};

// ---------- 后端配置快照 ----------
// 工作线程绝不直接读 Settings::shared()（主线程同时改设置会数据竞争），
// 发起请求时在调用线程拍快照带走。

// ---------- 活跃请求管理 ----------

struct ActiveRequest {
    std::shared_ptr<std::atomic<bool>> cancel;
};

std::mutex g_mu;
std::unordered_map<uint64_t, ActiveRequest> g_active;
std::atomic<uint64_t> g_nextId{1};

void finishRequest(uint64_t id) {
    std::lock_guard lock(g_mu);
    g_active.erase(id);
}

// ---------- provider 实现（工作线程内运行）----------

struct StreamOutcome {
    bool ok = false;
    std::wstring error;
    std::wstring full;
};

StreamOutcome runProvider(const TranslationRequest& req, const ProviderConfig& config,
                          const std::atomic<bool>& cancel,
                          std::optional<std::chrono::steady_clock::time_point>& firstToken) {
    StreamOutcome out;
    provider::Request wire;
    if (!provider::BuildRequest(config, req, wire, out.error)) return out;
    provider::Response response(config.kind);
    LineAssembler lines;
    auto onLine = [&](const std::string& line) {
        response.line(line);
        if (!response.text.empty() && !firstToken) firstToken = std::chrono::steady_clock::now();
    };
    auto receive = [&](const char* data, size_t len) { lines.feed(data, len, onLine); };
    auto result = http::PostStream(wire.url, wire.body, wire.headers, receive, cancel);
    if (config.kind == TranslationBackend::Ollama && result.status == 400 && !cancel.load()) {
        response = provider::Response(config.kind); lines = LineAssembler(); firstToken.reset();
        provider::BuildRequest(config, req, wire, out.error, false);
        result = http::PostStream(wire.url, wire.body, wire.headers, receive, cancel);
    }
    lines.finish(onLine);
    if (!result.ok) out.error = util::Widen(result.error);
    else if (!response.error.empty()) out.error = response.error;
    else if (!response.complete) out.error = L"Connection ended before the translation was complete.";
    else if (util::Trim(response.text).empty()) out.error = L"Empty translation.";
    else { out.ok = true; out.full = util::Trim(response.text); }
    return out;
}

std::string providerCacheIdentity(const std::vector<ProviderConfig>& providers) {
    json::Array identity;
    for (const auto& p : providers)
        identity.emplace_back(json::Object{{"id", p.id}, {"kind", ProviderKindCode(p.kind)},
            {"url", p.baseURL}, {"model", p.model}, {"key", p.apiKey}});
    // Memory only; never written to diagnostics or disk.
    return json::Value(std::move(identity)).dump();
}

} // namespace

const char* DefaultPromptTemplate() {
    return "You are a professional translation engine. Translate the user's text from {source} into {target}.{style} Output ONLY the translation, with no quotes, no explanations, no extra notes. Preserve the original meaning and formatting.";
}
std::string RenderPrompt(const TranslationRequest& req) {
    const std::string pattern = req.prompt.empty() ? DefaultPromptTemplate() : req.prompt;
    // Single pass: placeholder values are never interpreted as more templates.
    std::string result;
    for (size_t i = 0; i < pattern.size();) {
        if (pattern.compare(i, 8, "{source}") == 0) {
            result += req.source ? lang::PromptName(*req.source) : "the detected language"; i += 8;
        } else if (pattern.compare(i, 8, "{target}") == 0) {
            result += req.preserveLanguage ? "the same language as the original text" : lang::PromptName(req.target); i += 8;
        } else if (pattern.compare(i, 7, "{style}") == 0) {
            result += styleSuffix(req.style); i += 7;
        } else result += pattern[i++];
    }
    if (req.preserveLanguage) result += " Keep the original language; rewrite the text without translating it.";
    return result;
}

std::string SystemPrompt(Language target, std::optional<Language> source, RewriteStyle style) {
    std::string from = source ? lang::PromptName(*source) : "the detected language";
    std::string s = "You are a professional translation engine. Translate the user's text from ";
    s += from;
    s += " into ";
    s += lang::PromptName(target);
    s += ".";
    s += styleSuffix(style);
    s += " Output ONLY the translation, with no quotes, no explanations, no extra notes. "
         "Preserve the original meaning and formatting.";
    return s;
}

std::string PlainPrompt(const std::wstring& text, Language target, std::optional<Language> source, RewriteStyle style) {
    std::string from = source ? lang::PromptName(*source) : "the source language";
    std::string s = "Translate the following text from ";
    s += from;
    s += " to ";
    s += lang::PromptName(target);
    s += ".";
    s += styleSuffix(style);
    s += " Only output the translation, no explanation.\n\n";
    s += util::Narrow(text);
    return s;
}

uint64_t StreamWithProviders(const TranslationRequest& req, std::vector<ProviderConfig> configs,
                             DeltaFn onDelta, DoneFn onDone, bool useCache) {
    uint64_t id = g_nextId.fetch_add(1);
    const auto start = std::chrono::steady_clock::now();

    // 配置快照在调用线程（主线程）拍好带进工作线程
    const std::string identity = providerCacheIdentity(configs);

    // 缓存命中：同步回调全文
    std::string key = cacheKey(req, identity.c_str());
    if (auto cached = useCache ? Cache::shared().get(key) : std::nullopt) {
        onDelta(id, *cached);
        onDone(id, true, L"");
        diagnostics::Record("translation", "cacheHit",
                            "providerChain",
                            {}, {}, 0, 0, (int)req.text.size());
        return id;
    }

    auto cancel = std::make_shared<std::atomic<bool>>(false);
    {
        std::lock_guard lock(g_mu);
        g_active[id] = {cancel};
    }

    std::thread([req, configs = std::move(configs), id, key, cancel, start, useCache,
                 onDelta = std::move(onDelta), onDone = std::move(onDone)] {
        std::optional<std::chrono::steady_clock::time_point> firstToken;
        StreamOutcome out;
        bool primarySucceeded = false;
        std::wstring failures;
        const char* backend = "providerChain";
        for (size_t index = 0; index < configs.size() && !cancel->load(); ++index) {
            firstToken.reset();
            const auto attemptStart = std::chrono::steady_clock::now();
            out = runProvider(req, configs[index], *cancel, firstToken);
            backend = ProviderKindCode(configs[index].kind);
            primarySucceeded = index == 0 && out.ok;
            if (out.ok || cancel->load()) break;
            if (!failures.empty()) failures += L"\n";
            failures += std::to_wstring(index + 1) + L". " + util::Widen(configs[index].name) + L": " + out.error;
            const int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - attemptStart).count();
            diagnostics::Record("providerAttempt", "failure", backend, {}, "provider", -1, elapsed);
        }
        if (!out.ok) out.error = configs.empty() ? L"Add a provider in Settings > Providers first."
            : L"All providers failed.\n" + failures;
        bool cancelled = cancel->load();
        finishRequest(id);
        const auto end = std::chrono::steady_clock::now();
        const int totalMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
        const int firstMs = firstToken
            ? (int)std::chrono::duration_cast<std::chrono::milliseconds>(*firstToken - start).count()
            : -1;
        if (cancelled) {
            diagnostics::Record("translation", "cancelled", backend, {}, "cancelled",
                                firstMs, totalMs, (int)req.text.size());
            return;  // 取消的请求不再回调
        }
        const std::wstring cleaned = util::Trim(out.full);
        if (out.ok && !cleaned.empty()) {
            // A fallback result must not prevent the primary being retried next time.
            if (useCache && primarySucceeded) Cache::shared().set(key, cleaned);
            onDelta(id, cleaned);
            onDone(id, true, L"");
            diagnostics::Record("translation", "success", backend, {}, {},
                                firstMs, totalMs, (int)req.text.size());
        } else {
            const bool emptyResponse = out.ok && cleaned.empty();
            const std::wstring error = emptyResponse ? L"Empty translation" : out.error;
            onDone(id, false, error);
            diagnostics::Record("translation", "failure", backend, {},
                                emptyResponse ? "emptyResponse" : "provider",
                                firstMs, totalMs, (int)req.text.size());
        }
    }).detach();

    return id;
}

uint64_t Stream(const TranslationRequest& req, DeltaFn onDelta, DoneFn onDone) {
    return StreamWithProviders(req, Settings::shared().effectiveProviders(), std::move(onDelta), std::move(onDone), true);
}

void Cancel(uint64_t reqId) {
    std::lock_guard lock(g_mu);
    auto it = g_active.find(reqId);
    if (it != g_active.end()) it->second.cancel->store(true);
}

void CancelAll() {
    std::lock_guard lock(g_mu);
    for (auto& [id, req] : g_active) req.cancel->store(true);
}

std::optional<std::wstring> TranslateFully(const TranslationRequest& req, std::wstring* error) {
    std::mutex mu;
    std::condition_variable cv;
    bool done = false, ok = false;
    std::wstring full, err;

    Stream(
        req,
        [&](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(mu);
            full += delta;
        },
        [&](uint64_t, bool success, const std::wstring& e) {
            std::lock_guard lock(mu);
            ok = success;
            err = e;
            done = true;
            cv.notify_all();
        });

    std::unique_lock lock(mu);
    cv.wait(lock, [&] { return done; });

    std::wstring cleaned = util::Trim(full);
    if (!ok || cleaned.empty()) {
        if (error) *error = ok ? L"Empty translation" : err;
        return std::nullopt;
    }
    return cleaned;
}

void CheckHealthAsync(std::function<void(HealthCheckResult)> completion, std::optional<ProviderConfig> selected) {
    const auto start = std::chrono::steady_clock::now();
    const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    struct State {
        std::mutex mutex;
        std::wstring full;
        std::optional<std::chrono::steady_clock::time_point> first;
    };
    auto state = std::make_shared<State>();
    const std::string backend = selected ? ProviderKindCode(selected->kind) : "providerChain";
    auto configs = selected ? std::vector<ProviderConfig>{*selected} : Settings::shared().effectiveProviders();
    StreamWithProviders(
        {L"TypeTide-Win health check " + std::to_wstring(stamp), Language::English,
         Language::Chinese, RewriteStyle::Faithful}, std::move(configs),
        [state](uint64_t, const std::wstring& delta) {
            std::lock_guard lock(state->mutex);
            if (!delta.empty() && !state->first) state->first = std::chrono::steady_clock::now();
            state->full += delta;
        },
        [state, start, backend, completion = std::move(completion)](
            uint64_t, bool success, const std::wstring& message) mutable {
            const auto end = std::chrono::steady_clock::now();
            std::lock_guard lock(state->mutex);
            HealthCheckResult result;
            result.ok = success && !util::Trim(state->full).empty();
            result.message = result.ok ? L"Connected and translated successfully."
                                       : (message.empty() ? L"No translation returned." : message);
            // UI callbacks now receive only complete results. Actual first-token
            // timing is recorded by the provider worker, not this completion callback.
            result.firstTokenMs = -1;
            result.totalMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
            diagnostics::Record("backendHealthCheck", result.ok ? "success" : "failure",
                                backend, {}, result.ok ? "" : "backend",
                                result.firstTokenMs, result.totalMs);
            completion(std::move(result));
        }, false);
}

void ClearCacheForTesting() {
    Cache::shared().clear();
}

std::optional<std::wstring> ParseOpenAISSELine(const std::string& line) {
    if (line.rfind("data:", 0) != 0) return std::nullopt;
    const std::string payload = util::Trim(line.substr(5));
    if (payload == "[DONE]") return std::nullopt;
    bool ok = false;
    json::Value value = json::Parse(payload, &ok);
    if (!ok) return std::nullopt;
    const json::Value& delta = value["choices"].at(0)["delta"]["content"];
    if (!delta.isString() || delta.asString().empty()) return std::nullopt;
    return util::Widen(delta.asString());
}

std::vector<std::string> AssembleLinesForTesting(const std::vector<std::string>& fragments) {
    LineAssembler assembler;
    std::vector<std::string> lines;
    auto append = [&](const std::string& line) { lines.push_back(line); };
    for (const auto& fragment : fragments) assembler.feed(fragment.data(), fragment.size(), append);
    assembler.finish(append);
    return lines;
}

} // namespace translator
