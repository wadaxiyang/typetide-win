#include "ProviderProtocol.h"
#include "Json.h"
#include "Util.h"
#include <winhttp.h>

namespace provider {
namespace {
std::string baseURL(std::string base) {
    while (!base.empty() && base.back() == '/') base.pop_back();
    return base;
}
std::string segment(const std::string& text) {
    constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.') out += c;
        else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
    }
    return out;
}
}
bool Validate(const ProviderConfig& p, std::wstring& error) {
    if (util::Trim(p.name).empty() || util::Trim(p.model).empty()) {
        error = L"Enter a name and model for this provider."; return false;
    }
    if (p.name.size() > 100 || p.model.size() > 256 || p.baseURL.size() > 2048 || p.apiKey.size() > 2000 ||
        p.apiKey.find_first_of("\r\n") != std::string::npos) {
        error = L"Provider fields are too long or contain invalid characters."; return false;
    }
    const auto url = util::Widen(p.baseURL);
    URL_COMPONENTS parts{sizeof(parts)};
    parts.dwHostNameLength = parts.dwUserNameLength = parts.dwPasswordLength = parts.dwExtraInfoLength = (DWORD)-1;
    if (!WinHttpCrackUrl(url.c_str(), (DWORD)url.size(), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTP && parts.nScheme != INTERNET_SCHEME_HTTPS) ||
        !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength || parts.dwExtraInfoLength) {
        error = L"Enter an HTTP(S) base URL without credentials, a query, or a fragment."; return false;
    }
    const std::wstring host = util::ToLower(std::wstring(parts.lpszHostName, parts.dwHostNameLength));
    const bool local = host == L"localhost" || host == L"127.0.0.1" || host == L"[::1]" || host == L"::1";
    if (!p.apiKey.empty() && parts.nScheme != INTERNET_SCHEME_HTTPS && !local) {
        error = L"Use HTTPS when sending an API key to a remote provider."; return false;
    }
    if (p.kind != TranslationBackend::Ollama && p.apiKey.empty() && !local) {
        error = L"Enter an API key for this provider."; return false;
    }
    return true;
}
bool BuildRequest(const ProviderConfig& p, const TranslationRequest& input,
                  Request& out, std::wstring& error, bool disableThink) {
    if (!Validate(p, error)) return false;
    auto base = baseURL(p.baseURL);
    json::Object body;
    const auto system = translator::RenderPrompt(input);
    const auto text = util::Narrow(input.text);
    out = {};
    switch (p.kind) {
    case TranslationBackend::Ollama:
        out.url = util::Widen(base + "/api/generate");
        body = {{"model", p.model}, {"prompt", system + "\n\n" + text},
                {"stream", true}, {"keep_alive", "10m"}, {"options", json::Object{{"temperature", 0.3}}}};
        if (disableThink) body["think"] = false;
        if (!p.apiKey.empty()) out.headers = L"Authorization: Bearer " + util::Widen(p.apiKey) + L"\r\n";
        break;
    case TranslationBackend::OpenAI:
        out.url = util::Widen(base + "/chat/completions");
        body = {{"model", p.model}, {"stream", true}, {"messages", json::Array{
            json::Object{{"role", "system"}, {"content", system}},
            json::Object{{"role", "user"}, {"content", text}}}}};
        if (!p.apiKey.empty()) out.headers = L"Authorization: Bearer " + util::Widen(p.apiKey) + L"\r\n";
        break;
    case TranslationBackend::Anthropic:
        out.url = util::Widen(base + "/messages");
        out.headers = L"anthropic-version: 2023-06-01\r\nx-api-key: " + util::Widen(p.apiKey) + L"\r\n";
        body = {{"model", p.model}, {"stream", true}, {"max_tokens", 8192}, {"system", system},
                {"messages", json::Array{json::Object{{"role", "user"}, {"content", text}}}}};
        break;
    case TranslationBackend::Gemini: {
        auto model = p.model;
        if (model.rfind("models/", 0) == 0) model.erase(0, 7);
        out.url = util::Widen(base + "/models/" + segment(model) + ":streamGenerateContent?alt=sse");
        out.headers = L"x-goog-api-key: " + util::Widen(p.apiKey) + L"\r\n";
        body = {{"systemInstruction", json::Object{{"parts", json::Array{json::Object{{"text", system}}}}}},
                {"contents", json::Array{json::Object{{"role", "user"},
                    {"parts", json::Array{json::Object{{"text", text}}}}}}}};
        break;
    }
    }
    out.body = json::Value(std::move(body)).dump();
    return true;
}
void Response::line(const std::string& raw) {
    if (!error.empty()) return;
    std::string payload = raw;
    if (kind_ != TranslationBackend::Ollama) {
        if (raw.rfind("data:", 0) != 0) return;
        payload = util::Trim(raw.substr(5));
        if (payload == "[DONE]" && kind_ == TranslationBackend::OpenAI) { complete = true; return; }
    }
    bool valid = false;
    const auto v = json::Parse(payload, &valid);
    if (!valid || !v.isObject()) { error = L"Invalid response from provider."; return; }
    if (!v["error"].isNull() || v["type"].asString() == "error") {
        error = L"Provider returned an error."; return;
    }
    std::string delta;
    switch (kind_) {
    case TranslationBackend::Ollama:
        delta = v["response"].asString();
        if (v["done"].asBool()) complete = true;
        if (v["done_reason"].asString() == "length") error = L"Provider reached its output limit.";
        break;
    case TranslationBackend::OpenAI: {
        const auto& choice = v["choices"].at(0);
        delta = choice["delta"]["content"].asString();
        const auto reason = choice["finish_reason"].asString();
        if (reason == "stop") complete = true;
        else if (!reason.empty()) error = L"Provider did not finish the translation (" + util::Widen(reason) + L").";
        break;
    }
    case TranslationBackend::Anthropic: {
        const auto type = v["type"].asString();
        if (type == "content_block_delta" && v["delta"]["type"].asString() == "text_delta")
            delta = v["delta"]["text"].asString();
        if (type == "content_block_start" && v["content_block"]["type"].asString() == "text")
            delta = v["content_block"]["text"].asString();
        if (type == "message_stop") complete = true;
        const auto reason = v["delta"]["stop_reason"].asString();
        if (!reason.empty() && reason != "end_turn" && reason != "stop_sequence")
            error = L"Provider did not finish the translation (" + util::Widen(reason) + L").";
        break;
    }
    case TranslationBackend::Gemini: {
        const auto& candidate = v["candidates"].at(0);
        if (const auto* parts = candidate["content"]["parts"].array())
            for (const auto& part : *parts) if (!part["thought"].asBool()) delta += part["text"].asString();
        const auto reason = candidate["finishReason"].asString();
        if (reason == "STOP") complete = true;
        else if (!reason.empty() || !v["promptFeedback"]["blockReason"].asString().empty())
            error = L"Provider stopped or blocked the translation.";
        break;
    }
    }
    if (text.size() + delta.size() > 4 * 1024 * 1024) { error = L"Provider response is too large."; return; }
    text += util::Widen(delta);
}
}
