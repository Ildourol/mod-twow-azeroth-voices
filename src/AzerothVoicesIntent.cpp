#include "AzerothVoicesIntent.h"
#include "AzerothVoicesConfig.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <unordered_set>

namespace AzerothVoices
{
    namespace
    {
        std::string ToLower(std::string str)
        {
            std::transform(str.begin(), str.end(), str.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return str;
        }

        std::vector<std::string> ExtractWords(std::string const& text)
        {
            std::vector<std::string> words;
            std::string current;
            for (char c : text)
            {
                if (std::isalnum(static_cast<unsigned char>(c)))
                {
                    current.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
                }
                else
                {
                    if (!current.empty())
                    {
                        words.push_back(current);
                        current.clear();
                    }
                }
            }
            if (!current.empty())
                words.push_back(current);
            return words;
        }
    }

    bool IsPluralGroupToken(std::string const& word)
    {
        static std::unordered_set<std::string> const s_pluralTokens = {
            "everyone", "everybody", "all", "team", "party", "guild",
            "guys", "y'all", "yall", "anyone", "anybody", "folks",
            "group", "chat", "peeps"
        };
        return s_pluralTokens.count(word) > 0;
    }

    bool ContainsGroupQuestionPhrase(std::string const& lower)
    {
        static std::vector<std::string> const s_phrases = {
            "what should we", "where should we", "who wants", "who needs",
            "ready?", "rdy?", "ready", "rdy", "who has", "anyone have",
            "what's the plan", "whats the plan", "anyone ready", "thoughts?",
            "how do we", "where to", "which way"
        };
        for (auto const& phrase : s_phrases)
        {
            if (lower.find(phrase) != std::string::npos)
                return true;
        }
        return false;
    }

    MultiAddressedDecision AnalyzeMessageIntent(std::string const& message,
                                                 std::vector<IntentCandidate> const& candidates,
                                                 bool isGroupOrGuildScope)
    {
        MultiAddressedDecision decision;
        if (message.empty())
            return decision;

        std::string const lowerMsg = ToLower(message);
        std::vector<std::string> const words = ExtractWords(message);

        // Check if message is a short casual greeting
        decision.isBriefCasual = IsShortGreetingMessage(message);

        // Pass 1: Match against candidate names (case-insensitive whole word)
        for (auto const& candidate : candidates)
        {
            if (candidate.name.empty())
                continue;

            std::string const candLower = ToLower(candidate.name);
            for (auto const& word : words)
            {
                if (word == candLower)
                {
                    decision.matchedNames.push_back(candidate.name);
                    if (decision.explicitAddressedGuid == 0)
                    {
                        decision.explicitAddressedGuid = candidate.guid;
                        decision.explicitAddressedName = candidate.name;
                    }
                    break;
                }
            }
        }

        // If multiple distinct candidate names are explicitly mentioned -> multi-addressed!
        if (decision.matchedNames.size() > 1)
        {
            decision.isMultiAddressed = true;
            return decision;
        }

        // If exactly one candidate name was explicitly mentioned -> single-bot target!
        if (decision.matchedNames.size() == 1)
        {
            decision.isMultiAddressed = false;
            return decision;
        }

        // Pass 2: No specific name mentioned. Check for plural tokens or group questions
        bool hasPluralToken = false;
        for (auto const& word : words)
        {
            if (IsPluralGroupToken(word))
            {
                hasPluralToken = true;
                break;
            }
        }

        bool const hasQuestionPhrase = ContainsGroupQuestionPhrase(lowerMsg);

        if ((hasPluralToken || hasQuestionPhrase) && (isGroupOrGuildScope || candidates.size() >= 2))
        {
            decision.isMultiAddressed = true;
        }
        else
        {
            decision.isMultiAddressed = false;
        }

        return decision;
    }

    std::string BuildMultiBotSystemPromptGuidance(std::vector<IntentCandidate> const& responders)
    {
        if (responders.empty())
            return "";

        std::ostringstream oss;
        oss << "MULTI-BOT CONVERSATION PROTOCOL: The player is addressing the entire group. "
            << "Respond on behalf of the following companions in order:\n";

        for (size_t i = 0; i < responders.size(); ++i)
        {
            oss << "  " << (i + 1) << ". " << responders[i].name;
            if (!responders[i].className.empty())
                oss << " (" << responders[i].className << ")";
            oss << '\n';
        }

        oss << "Output must strictly follow a JSON array of responses where each bot contributes "
            << "a unique, concise, in-character perspective:\n"
            << "[{\"speaker\":\"" << responders[0].name << "\",\"message\":\"...\"}";
        for (size_t i = 1; i < responders.size(); ++i)
        {
            oss << ",{\"speaker\":\"" << responders[i].name << "\",\"message\":\"...\"}";
        }
        oss << "]\nDo not repeat each other's words or sentiment.";

        return oss.str();
    }
}
