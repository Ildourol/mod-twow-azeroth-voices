#pragma once

#include "AzerothVoicesTypes.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <string>

namespace AzerothVoices
{
    // One delivery timeline per normalized automated-chat channel plus the
    // outdoor zone or map/instance identity. Direct player-directed replies are
    // never paced, and combat-start, event, boss, and dungeon/raid reactions
    // stay outside the quieter reductions.
    std::string NormalizePacingKey(ChatScope scope, std::string const& channelName,
                                   uint32_t mapId, uint32_t instanceId, uint32_t zoneId,
                                   bool instance);

    std::string NormalizeGuildPacingKey(uint32_t guildId);
    std::string NormalizeProximityPacingKey(uint32_t mapId, uint32_t instanceId, uint32_t zoneId, bool instance = false);

    // Overloaded convenience helper for estimated exchange duration
    uint32_t EstimateExchangeMilliseconds(size_t lines, uint32_t averageDelayMs,
                                          uint32_t speechDurationMs = 3000);

    // The full duration of a generated multi-line exchange is reserved before
    // its first line is scheduled, so a later automated exchange cannot
    // interleave into the middle of it.
    uint32_t EstimateExchangeMilliseconds(size_t lines, bool typingSimulation,
                                          uint32_t baseDelayMilliseconds,
                                          uint32_t perCharacterMilliseconds,
                                          uint32_t assumedCharactersPerLine,
                                          uint32_t interLineGapMilliseconds);

    class DeliveryTimeline
    {
    public:
        using TimePoint = std::chrono::steady_clock::time_point;

        struct LaneState
        {
            ExchangeId activeOwner{};
            bool hasActiveOwner = false;
            bool anyLineDelivered = false;
            TimePoint quietDeadline{};
            TimePoint priorQuietDeadline{};
            TimePoint lastDelivered{};
            uint32_t minimumGapMilliseconds = 0;
        };

        void Clear();
        size_t Size() const;

        bool CanDeliver(std::string const& key, TimePoint now, ExchangeId const& requester = {}) const;
        bool IsOwnedByAnother(std::string const& key, ExchangeId const& requester = {}) const;
        TimePoint NextEligible(std::string const& key, TimePoint now, ExchangeId const& requester = {}) const;
        TimePoint NextEligible(std::string const& key) const;

        bool Acquire(std::string const& key, ExchangeId const& owner, TimePoint now,
                     uint32_t minimumGapMilliseconds);
        void OnLineDelivered(std::string const& key, ExchangeId const& owner, TimePoint now);
        void Release(std::string const& key, ExchangeId const& owner, TimePoint now,
                     bool lineDelivered = false);

        void Reserve(std::string const& key, TimePoint now, uint32_t durationMilliseconds,
                     uint32_t minimumGapMilliseconds);
        void Prune(TimePoint now);

        // Deterministic, secret-free summary used by tests and status output.
        std::string Describe(TimePoint now) const;

    private:
        std::map<std::string, LaneState> m_lanes;
    };
}
