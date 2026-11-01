#pragma once

#include "AzerothVoicesConfig.h"
#include "AzerothVoicesPacing.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace AzerothVoices
{
    // PartyGate classification policy for per-party delivery gating
    enum class PartyGatePolicy : uint8_t
    {
        Bypass,
        Filler,
        Contextual,
        Responsive,
        Urgent
    };

    char const* PartyGatePolicyName(PartyGatePolicy policy);

    // Determines the PartyGate policy from the trigger string.
    PartyGatePolicy PartyGatePolicyForTrigger(std::string const& trigger);

    // Resolves the configured minimum gap in seconds for a given policy.
    uint32_t PartyGateGapSeconds(PartyGatePolicy policy, Config const& config);

    // Generates the party pacing key: "party:<groupId>" or "party:<groupId>:subgroup:<subgroup>"
    std::string PartyPacingKey(uint32_t groupId, int32_t subgroup = -1);

    // Party schedule status for active-owner gating (R2)
    enum class PartyScheduleStatus : uint8_t
    {
        Ready,
        BlockedByActiveOwner
    };

    struct PartyScheduleResult
    {
        PartyScheduleStatus status = PartyScheduleStatus::Ready;
        std::chrono::steady_clock::time_point scheduledTime{};

        // Implicit conversion to time_point allows seamless compatibility with callers and tests
        operator std::chrono::steady_clock::time_point() const { return scheduledTime; }
        bool IsBlocked() const { return status == PartyScheduleStatus::BlockedByActiveOwner; }
    };

    // Pre-LLM deferral check for Filler chatter (Section 28).
    // Returns true if congested filler should be deferred before spending an LLM call.
    bool ShouldDeferPartyFiller(DeliveryTimeline const& timeline,
                                std::string const& partyKey,
                                PartyGatePolicy policy,
                                uint32_t deferThresholdSeconds,
                                std::chrono::steady_clock::time_point now,
                                uint32_t* outWaitSeconds = nullptr);

    // Calculates the adjusted schedule time for a Party message (Section 23, 27, 29, R2).
    PartyScheduleResult CalculatePartyScheduledTime(
        DeliveryTimeline const& timeline,
        std::string const& partyKey,
        PartyGatePolicy policy,
        std::chrono::steady_clock::time_point requestedTime,
        std::chrono::steady_clock::time_point now,
        uint32_t maxFillerDelaySeconds,
        ExchangeId const& owner = {});
}
