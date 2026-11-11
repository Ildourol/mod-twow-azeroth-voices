#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices
{
    struct IntentCandidate
    {
        uint64_t guid = 0;
        std::string name;
        std::string className;
        std::string race;
    };

    struct MultiAddressedDecision
    {
        bool isMultiAddressed = false;
        bool isBriefCasual = false;
        bool requiresReply = true;
        uint64_t explicitAddressedGuid = 0;
        std::string explicitAddressedName;
        std::vector<std::string> matchedNames;
    };

    // Checks if a lowercase single word is a plural/collective address token
    bool IsPluralGroupToken(std::string const& word);

    // Checks if the lowercase message contains a collective question or group inquiry phrase
    bool ContainsGroupQuestionPhrase(std::string const& lower);

    // Fast-path lexical analyzer determining whether a chat message is directed at
    // a specific single bot, or is multi-addressed to the party, guild, or local say audience.
    MultiAddressedDecision AnalyzeMessageIntent(std::string const& message,
                                                 std::vector<IntentCandidate> const& candidates,
                                                 bool isGroupOrGuildScope = true);

    // Builds a multi-bot conversation system prompt for Architecture B (one-shot JSON array)
    std::string BuildMultiBotSystemPromptGuidance(std::vector<IntentCandidate> const& responders);
}
