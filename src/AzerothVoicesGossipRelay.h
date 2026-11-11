#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace AzerothVoices
{
    struct GossipItem
    {
        std::string channel; // e.g. "world", "trade", "barrens"
        std::string topic;   // e.g. "mankrik_wife", "loot_drama", "chuck_norris"
        std::string speaker; // Speaker name in World chat
        std::string snippet; // Representative dialogue snippet
        uint32_t zoneId = 0;
        uint32_t mapId = 0;
        uint32_t faction = 2; // 0 = alliance, 1 = horde, 2 = neutral/any
        uint32_t timestamp = 0;
    };

    class GossipRelayEngine
    {
    public:
        void RecordGossip(GossipItem item);
        GossipItem const* GetLatestGossip(uint32_t zoneId = 0, uint32_t mapId = 0, uint32_t faction = 2) const;

        bool CanRelayToParty(uint32_t groupId, uint32_t nowSeconds, uint32_t cooldownSeconds,
                             uint32_t chancePercent, uint32_t roll100) const;
        bool CanRelayToSay(uint64_t playerGuid, uint32_t nowSeconds, uint32_t cooldownSeconds,
                           uint32_t chancePercent, uint32_t roll100) const;
        bool CanRelayToGuild(uint32_t guildId, uint32_t nowSeconds, uint32_t cooldownSeconds,
                            uint32_t chancePercent, uint32_t roll100) const;

        void NoteRelay(std::string const& key, uint32_t nowSeconds);
        std::string BuildGossipPrompt(GossipItem const& item, std::string const& scopeName) const;
        void Prune(uint32_t nowSeconds, uint32_t maxTtlSeconds = 3600);
        void Clear();
        size_t HistorySize() const;

    private:
        std::deque<GossipItem> m_recentGossip;
        std::unordered_map<std::string, uint32_t> m_lastRelayByTarget;
    };
}
