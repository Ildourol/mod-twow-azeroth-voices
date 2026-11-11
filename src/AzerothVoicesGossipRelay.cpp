#include "AzerothVoicesGossipRelay.h"

#include <algorithm>
#include <sstream>

namespace AzerothVoices
{
    void GossipRelayEngine::RecordGossip(GossipItem item)
    {
        if (item.snippet.empty() && item.topic.empty())
            return;

        m_recentGossip.push_front(std::move(item));
        while (m_recentGossip.size() > 20)
            m_recentGossip.pop_back();
    }

    GossipItem const* GossipRelayEngine::GetLatestGossip(uint32_t zoneId, uint32_t mapId, uint32_t faction) const
    {
        for (auto const& item : m_recentGossip)
        {
            if (zoneId != 0 && item.zoneId != 0 && item.zoneId != zoneId)
                continue;
            if (mapId != 0 && item.mapId != 0 && item.mapId != mapId)
                continue;
            if (faction != 2 && item.faction != 2 && item.faction != faction)
                continue;
            return &item;
        }

        // Fallback: return most recent gossip item regardless of location
        return m_recentGossip.empty() ? nullptr : &m_recentGossip.front();
    }

    bool GossipRelayEngine::CanRelayToParty(uint32_t groupId, uint32_t nowSeconds, uint32_t cooldownSeconds,
                                            uint32_t chancePercent, uint32_t roll100) const
    {
        if (groupId == 0 || chancePercent == 0 || (roll100 % 100) >= chancePercent)
            return false;

        std::string const key = "party:" + std::to_string(groupId);
        auto it = m_lastRelayByTarget.find(key);
        if (it != m_lastRelayByTarget.end() && nowSeconds < it->second + cooldownSeconds)
            return false;

        return !m_recentGossip.empty();
    }

    bool GossipRelayEngine::CanRelayToSay(uint64_t playerGuid, uint32_t nowSeconds, uint32_t cooldownSeconds,
                                          uint32_t chancePercent, uint32_t roll100) const
    {
        if (playerGuid == 0 || chancePercent == 0 || (roll100 % 100) >= chancePercent)
            return false;

        std::string const key = "say:" + std::to_string(playerGuid);
        auto it = m_lastRelayByTarget.find(key);
        if (it != m_lastRelayByTarget.end() && nowSeconds < it->second + cooldownSeconds)
            return false;

        return !m_recentGossip.empty();
    }

    bool GossipRelayEngine::CanRelayToGuild(uint32_t guildId, uint32_t nowSeconds, uint32_t cooldownSeconds,
                                           uint32_t chancePercent, uint32_t roll100) const
    {
        if (guildId == 0 || chancePercent == 0 || (roll100 % 100) >= chancePercent)
            return false;

        std::string const key = "guild:" + std::to_string(guildId);
        auto it = m_lastRelayByTarget.find(key);
        if (it != m_lastRelayByTarget.end() && nowSeconds < it->second + cooldownSeconds)
            return false;

        return !m_recentGossip.empty();
    }

    void GossipRelayEngine::NoteRelay(std::string const& key, uint32_t nowSeconds)
    {
        if (!key.empty())
            m_lastRelayByTarget[key] = nowSeconds;
    }

    std::string GossipRelayEngine::BuildGossipPrompt(GossipItem const& item, std::string const& scopeName) const
    {
        std::ostringstream oss;
        oss << "WORLD CHAT GOSSIP RELAY: You just overheard this exchange in World chat: ";

        if (!item.speaker.empty())
            oss << "[" << item.speaker << "]: ";

        if (!item.snippet.empty())
            oss << "\"" << item.snippet << "\" ";
        else if (!item.topic.empty())
            oss << "(discussion regarding " << item.topic << ") ";

        oss << "\nMake a brief, witty, in-character remark about this drama in " << scopeName
            << " chat. Keep it concise (1-2 sentences). Do not repeat the quote verbatim; react to it.";

        return oss.str();
    }

    void GossipRelayEngine::Prune(uint32_t nowSeconds, uint32_t maxTtlSeconds)
    {
        for (auto it = m_lastRelayByTarget.begin(); it != m_lastRelayByTarget.end();)
        {
            if (nowSeconds >= it->second + maxTtlSeconds)
                it = m_lastRelayByTarget.erase(it);
            else
                ++it;
        }
    }

    void GossipRelayEngine::Clear()
    {
        m_recentGossip.clear();
        m_lastRelayByTarget.clear();
    }

    size_t GossipRelayEngine::HistorySize() const
    {
        return m_recentGossip.size();
    }
}
