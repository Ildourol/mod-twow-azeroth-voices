#include "AzerothVoicesChannelMembership.h"
#include <algorithm>
#include <cctype>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <utility>

namespace AzerothVoices { namespace ChannelMembership {
namespace {
struct Signal { uint64_t guid; std::string channel; bool joined; };
std::mutex mutex;
std::deque<Signal> pending;
std::map<uint64_t, std::set<std::string>> members;
bool overflow = false;
constexpr size_t MaximumPending = 8192;
constexpr size_t MaximumPlayers = 16384;
constexpr size_t MaximumChannelsPerPlayer = 64;
std::string Normalize(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return name;
}
}
void Observe(uint64_t guid, std::string const& channel, bool joined) {
    if (!guid || channel.empty() || channel.size() > 255) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (pending.size() >= MaximumPending) { pending.clear(); overflow = true; }
    pending.push_back({guid, Normalize(channel), joined});
}
void Logout(uint64_t guid) {
    if (!guid) return;
    std::lock_guard<std::mutex> lock(mutex);
    if (pending.size() >= MaximumPending) { pending.clear(); overflow = true; }
    pending.push_back({guid, {}, false});
}
void Drain() {
    std::deque<Signal> batch;
    { std::lock_guard<std::mutex> lock(mutex);
      batch.swap(pending);
      if (overflow) { members.clear(); overflow = false; }
    }
    for (auto const& signal : batch) {
        if (signal.channel.empty()) { members.erase(signal.guid); continue; }
        auto it = members.find(signal.guid);
        if (!signal.joined) {
            if (it != members.end()) {
                it->second.erase(signal.channel);
                if (it->second.empty()) members.erase(it);
            }
        } else {
            if (it == members.end()) {
                if (members.size() >= MaximumPlayers) continue;
                it = members.emplace(signal.guid, std::set<std::string>{}).first;
            }
            if (it->second.size() < MaximumChannelsPerPlayer) it->second.insert(signal.channel);
        }
    }
}
bool Contains(uint64_t guid, std::string const& channel) {
    auto it = members.find(guid);
    return it != members.end() && it->second.count(Normalize(channel)) != 0;
}
void Clear() {
    std::lock_guard<std::mutex> lock(mutex);
    pending.clear(); members.clear(); overflow = false;
}
}}
