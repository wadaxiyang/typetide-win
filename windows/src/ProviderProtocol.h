#pragma once
#include "Settings.h"
#include "Translator.h"
#include <string>

namespace provider {
struct Request { std::wstring url, headers; std::string body; };
bool BuildRequest(const ProviderConfig& config, const TranslationRequest& input,
                  Request& output, std::wstring& error, bool disableThink = true);
bool Validate(const ProviderConfig& config, std::wstring& error);
// One parser per attempt. Failed/unfinished responses never reach the UI.
class Response {
public:
    explicit Response(TranslationBackend kind) : kind_(kind) {}
    void line(const std::string& line);
    std::wstring text, error;
    bool complete = false;
private:
    TranslationBackend kind_;
};
}
