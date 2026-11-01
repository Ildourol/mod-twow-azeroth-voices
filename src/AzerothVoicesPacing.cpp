#include "AzerothVoicesPacing.h"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <sstream>

namespace AzerothVoices
{
    namespace
    {
        std::string Lower(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        char const* ScopeToken(ChatScope scope)
        {
            switch (scope)
            {
                case ChatScope::World: return "world";
                case ChatScope::Channel: return "channel";
                default: break;
            }
            return "";
        }

        uint64_t MillisecondsBetween(DeliveryTimeline::TimePoint later,
                                     DeliveryTimeline::TimePoint earlier)
        {
            if (later <= earlier)
                return 0;
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(later - earlier).count());
        }
    }

    std::string NormalizePacingKey(ChatScope scope, std::string const& channelName,
                                   uint32_t mapId, uint32_t instanceId, uint32_t zoneId,
                                   bool instance)
    {
        char const* const scopeToken = ScopeToken(scope);
        if (!*scopeToken)
            return "";

        std::ostringstream key;
        key << scopeToken << ':';
        key << (channelName.empty() ? "world" : Lower(channelName));
        if (instance)
            key << "|map:" << mapId << ':' << instanceId;
        else
            key << "|zone:" << zoneId;
        return key.str();
    }

    std::string NormalizeGuildPacingKey(uint32_t guildId)
    {
        if (!guildId)
            return "";
        return "guild:" + std::to_string(guildId);
    }

    std::string NormalizeProximityPacingKey(uint32_t mapId, uint32_t instanceId, uint32_t zoneId, bool instance)
    {
        std::ostringstream key;
        key << "proximity:";
        if (instance || instanceId != 0)
            key << "map:" << mapId << ':' << instanceId;
        else
            key << "zone:" << zoneId;
        return key.str();
    }

    uint32_t EstimateExchangeMilliseconds(size_t lines, uint32_t averageDelayMs,
                                          uint32_t speechDurationMs)
    {
        if (!lines)
            return 0;
        uint64_t const total = static_cast<uint64_t>(lines) * (static_cast<uint64_t>(averageDelayMs) + speechDurationMs);
        return static_cast<uint32_t>(std::min<uint64_t>(total, 600000));
    }

    uint32_t EstimateExchangeMilliseconds(size_t lines, bool typingSimulation,
                                          uint32_t baseDelayMilliseconds,
                                          uint32_t perCharacterMilliseconds,
                                          uint32_t assumedCharactersPerLine,
                                          uint32_t interLineGapMilliseconds)
    {
        if (!lines)
            return 0;
        if (!typingSimulation)
            return interLineGapMilliseconds * static_cast<uint32_t>(lines - 1);

        uint64_t const perLine = static_cast<uint64_t>(baseDelayMilliseconds) +
            static_cast<uint64_t>(perCharacterMilliseconds) * assumedCharactersPerLine;
        uint64_t const total = perLine * lines +
            static_cast<uint64_t>(interLineGapMilliseconds) * (lines - 1);
        return static_cast<uint32_t>(std::min<uint64_t>(total, 600000));
    }

    void DeliveryTimeline::Clear()
    {
        m_lanes.clear();
    }

    size_t DeliveryTimeline::Size() const
    {
        return m_lanes.size();
    }

    bool DeliveryTimeline::CanDeliver(std::string const& key, TimePoint now, ExchangeId const& requester) const
    {
        if (key.empty())
            return true;
        auto found = m_lanes.find(key);
        if (found == m_lanes.end())
            return true;

        LaneState const& lane = found->second;
        if (lane.hasActiveOwner)
        {
            if (requester.IsValid() && lane.activeOwner == requester)
                return true;
            return false;
        }

        return now >= lane.quietDeadline;
    }

    bool DeliveryTimeline::IsOwnedByAnother(std::string const& key, ExchangeId const& requester) const
    {
        if (key.empty())
            return false;
        auto found = m_lanes.find(key);
        if (found == m_lanes.end())
            return false;

        LaneState const& lane = found->second;
        if (lane.hasActiveOwner)
        {
            if (requester.IsValid() && lane.activeOwner == requester)
                return false;
            return true;
        }
        return false;
    }

    DeliveryTimeline::TimePoint DeliveryTimeline::NextEligible(std::string const& key, TimePoint now, ExchangeId const& requester) const
    {
        if (key.empty())
            return now;
        auto found = m_lanes.find(key);
        if (found == m_lanes.end())
            return now;

        LaneState const& lane = found->second;
        if (lane.hasActiveOwner)
        {
            if (requester.IsValid() && lane.activeOwner == requester)
                return now;
            return std::max(now, lane.quietDeadline);
        }

        return std::max(now, lane.quietDeadline);
    }

    DeliveryTimeline::TimePoint DeliveryTimeline::NextEligible(std::string const& key) const
    {
        if (key.empty())
            return TimePoint{};
        auto found = m_lanes.find(key);
        if (found == m_lanes.end())
            return TimePoint{};
        return found->second.quietDeadline;
    }

    bool DeliveryTimeline::Acquire(std::string const& key, ExchangeId const& owner, TimePoint now,
                                   uint32_t minimumGapMilliseconds)
    {
        if (key.empty())
            return true;
        if (!owner.IsValid())
            return false;

        LaneState& lane = m_lanes[key];
        // R5: Idempotent Acquire by the same owner
        if (lane.hasActiveOwner && lane.activeOwner == owner)
            return true;

        if (!CanDeliver(key, now, owner))
            return false;

        lane.priorQuietDeadline = lane.quietDeadline;
        lane.hasActiveOwner = true;
        lane.activeOwner = owner;
        lane.anyLineDelivered = false;
        lane.lastDelivered = TimePoint{};
        lane.minimumGapMilliseconds = minimumGapMilliseconds;
        return true;
    }

    void DeliveryTimeline::OnLineDelivered(std::string const& key, ExchangeId const& owner, TimePoint now)
    {
        if (!owner.IsValid())
            return;
        auto handleLane = [&](LaneState& lane) {
            if (lane.hasActiveOwner && lane.activeOwner == owner)
            {
                lane.anyLineDelivered = true;
                lane.lastDelivered = (lane.lastDelivered.time_since_epoch().count() == 0)
                    ? now : std::max(lane.lastDelivered, now);
                TimePoint const minQuiet = lane.lastDelivered + std::chrono::milliseconds(lane.minimumGapMilliseconds);
                lane.quietDeadline = std::max(lane.quietDeadline, minQuiet);
            }
        };

        if (!key.empty())
        {
            auto found = m_lanes.find(key);
            if (found != m_lanes.end())
                handleLane(found->second);
            return;
        }
        for (auto& pair : m_lanes)
            handleLane(pair.second);
    }

    void DeliveryTimeline::Release(std::string const& key, ExchangeId const& owner, TimePoint now,
                                   bool lineDelivered)
    {
        if (!owner.IsValid())
            return;
        auto handleLane = [&](LaneState& lane) {
            if (lane.hasActiveOwner && lane.activeOwner == owner)
            {
                if (lineDelivered || lane.anyLineDelivered)
                {
                    // R5: Compute quiet expiry from the last delivery (lastDelivered + captured gap)!
                    TimePoint const deliveryAnchor = (lane.lastDelivered.time_since_epoch().count() != 0)
                        ? lane.lastDelivered : now;
                    TimePoint const deliveryQuiet = deliveryAnchor + std::chrono::milliseconds(lane.minimumGapMilliseconds);
                    lane.quietDeadline = std::max(lane.priorQuietDeadline, deliveryQuiet);
                }
                else
                {
                    // R5: Rollback for unperformed/cancelled exchange: restore prior independent quiet deadline
                    lane.quietDeadline = lane.priorQuietDeadline;
                }
                lane.hasActiveOwner = false;
                lane.activeOwner = ExchangeId{};
            }
        };

        if (!key.empty())
        {
            auto found = m_lanes.find(key);
            if (found != m_lanes.end())
                handleLane(found->second);
            return;
        }
        for (auto& pair : m_lanes)
            handleLane(pair.second);
    }

    void DeliveryTimeline::Reserve(std::string const& key, TimePoint now,
                                   uint32_t durationMilliseconds, uint32_t minimumGapMilliseconds)
    {
        if (key.empty())
            return;
        TimePoint candidate = now + std::chrono::milliseconds(
            static_cast<int64_t>(durationMilliseconds) + static_cast<int64_t>(minimumGapMilliseconds));
        LaneState& lane = m_lanes[key];
        // R5: Other Reserve calls must not overwrite an active owner's policy
        if (!lane.hasActiveOwner)
        {
            if (lane.quietDeadline < candidate)
                lane.quietDeadline = candidate;
            lane.minimumGapMilliseconds = minimumGapMilliseconds;
        }
        else
        {
            if (lane.quietDeadline < candidate)
                lane.quietDeadline = candidate;
        }
    }

    void DeliveryTimeline::Prune(TimePoint now)
    {
        for (auto it = m_lanes.begin(); it != m_lanes.end(); )
        {
            if (!it->second.hasActiveOwner && it->second.quietDeadline <= now)
                it = m_lanes.erase(it);
            else
                ++it;
        }
    }

    std::string DeliveryTimeline::Describe(TimePoint now) const
    {
        std::ostringstream text;
        for (auto const& entry : m_lanes)
        {
            if (text.tellp() > 0)
                text << ',';
            uint64_t const remaining = MillisecondsBetween(entry.second.quietDeadline, now);
            text << entry.first << '=' << (remaining / 1000);
            if (entry.second.hasActiveOwner)
                text << "(active)";
        }
        return text.str();
    }
}
