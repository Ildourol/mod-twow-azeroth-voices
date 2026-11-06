#pragma once
#include <cstdint>
#include <string>

namespace AzerothVoices { namespace ChannelMembership {
// Producers may run on packet threads. Only Drain/Contains/Clear run on world thread.
std::string Normalize(std::string name);
void Observe(uint64_t guid, std::string const& channel, bool joined);
void Logout(uint64_t guid);
void Drain();
bool Contains(uint64_t guid, std::string const& channel);
bool ContainsNormalized(uint64_t guid, std::string const& normalizedChannel);
void Clear();
}}
