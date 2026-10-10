#include "Credentials.h"
#include <windows.h>
#include <wincred.h>
#include "Util.h"

namespace {
constexpr wchar_t kTarget[] = L"TypeTide-Win/OpenAICompatibleApiKey";
}

namespace credentials {

static std::string load(const wchar_t* target) {
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(target, CRED_TYPE_GENERIC, 0, &credential)) return {};

    std::string key;
    if (credential->CredentialBlob && credential->CredentialBlobSize) {
        const char* bytes = reinterpret_cast<const char*>(credential->CredentialBlob);
        key.assign(bytes, bytes + credential->CredentialBlobSize);
    }
    CredFree(credential);
    return key;
}

static bool save(const wchar_t* target, const std::string& key) {
    if (key.empty()) {
        return CredDeleteW(target, CRED_TYPE_GENERIC, 0) || GetLastError() == ERROR_NOT_FOUND;
    }

    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<LPWSTR>(target);
    credential.CredentialBlobSize = static_cast<DWORD>(key.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(key.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<LPWSTR>(L"TypeTide-Win");
    return CredWriteW(&credential, 0) != FALSE;
}

std::string LoadCloudApiKey() { return load(kTarget); }
bool SaveCloudApiKey(const std::string& key) { return save(kTarget, key); }
std::string LoadProviderKey(const std::string& id) {
    return load((L"TypeTide-Win/Provider/" + util::Widen(id)).c_str());
}
bool SaveProviderKey(const std::string& id, const std::string& key) {
    return save((L"TypeTide-Win/Provider/" + util::Widen(id)).c_str(), key);
}
} // namespace credentials
