#ifndef AZEROTH_VOICES_HTTP_POLICY_H
#define AZEROTH_VOICES_HTTP_POLICY_H

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>

namespace AzerothVoices
{
    // Explicit operator opt-in for plaintext HTTP to a trusted Docker/VM host.
    // Match whole host names only; never infer trust from DNS or a suffix.
    inline bool IsAllowedHttpHost(std::string host, std::string const& allowedHosts)
    {
        auto normalize = [](std::string value) {
            auto notSpace = [](unsigned char c) { return !std::isspace(c); };
            value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
            value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        };
        host = normalize(host);
        if (host.empty())
            return false;
        std::istringstream entries(allowedHosts);
        std::string entry;
        while (std::getline(entries, entry, ','))
            if (normalize(entry) == host)
                return true;
        return false;
    }
}

#endif
