#pragma once

#include <string>

namespace credentials {

std::string LoadCloudApiKey();
bool SaveCloudApiKey(const std::string& key);
std::string LoadProviderKey(const std::string& id);
bool SaveProviderKey(const std::string& id, const std::string& key);

} // namespace credentials
