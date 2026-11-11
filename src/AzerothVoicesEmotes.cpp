#include "AzerothVoicesEmotes.h"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AzerothVoices
{
    namespace
    {
        bool IsValidUtf8String(std::string const& s)
        {
            size_t i = 0;
            while (i < s.size())
            {
                unsigned char c = static_cast<unsigned char>(s[i]);
                if (c <= 0x7F)
                {
                    ++i;
                }
                else if ((c & 0xE0) == 0xC0)
                {
                    if (i + 1 >= s.size() || (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80)
                        return false;
                    i += 2;
                }
                else if ((c & 0xF0) == 0xE0)
                {
                    if (i + 2 >= s.size() ||
                        (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                        (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80)
                        return false;
                    i += 3;
                }
                else if ((c & 0xF8) == 0xF0)
                {
                    if (i + 3 >= s.size() ||
                        (static_cast<unsigned char>(s[i + 1]) & 0xC0) != 0x80 ||
                        (static_cast<unsigned char>(s[i + 2]) & 0xC0) != 0x80 ||
                        (static_cast<unsigned char>(s[i + 3]) & 0xC0) != 0x80)
                        return false;
                    i += 4;
                }
                else
                    return false;
            }
            return true;
        }

        std::string TrimWhitespace(std::string const& value)
        {
            size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
                ++start;
            size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
                --end;
            return value.substr(start, end - start);
        }

        struct ParsedEffectsData
        {
            bool hasSpeaker = false;
            std::string speakerVal;
            bool hasListener = false;
            std::string listenerVal;
            bool hasRoleplay = false;
            std::string roleplayVal;
            bool valid = false;
        };

        ParsedEffectsData ParseEffectsJson(std::string const& jsonStr)
        {
            ParsedEffectsData result;
            try
            {
                auto parsed = nlohmann::json::parse(jsonStr);
                if (!parsed.is_object())
                    return result;

                for (auto it = parsed.begin(); it != parsed.end(); ++it)
                {
                    std::string const& k = it.key();
                    if (k != "speaker" && k != "listener" && k != "roleplay")
                        return result; // unknown key rejection

                    if (!it.value().is_string())
                        return result; // all effects fields must be strings

                    if (k == "speaker")
                    {
                        result.hasSpeaker = true;
                        result.speakerVal = it.value().get<std::string>();
                    }
                    else if (k == "listener")
                    {
                        result.hasListener = true;
                        result.listenerVal = it.value().get<std::string>();
                    }
                    else if (k == "roleplay")
                    {
                        result.hasRoleplay = true;
                        result.roleplayVal = it.value().get<std::string>();
                    }
                }
                result.valid = true;
            }
            catch (...)
            {
                return result;
            }
            return result;
        }
    }

    bool TryParseSpeakerGesture(std::string const& token, EmoteGesture& out)
    {
        if (token == "none")
            out = EmoteGesture::None;
        else if (token == "laugh")
            out = EmoteGesture::Laugh;
        else if (token == "applaud")
            out = EmoteGesture::Applaud;
        else if (token == "bow")
            out = EmoteGesture::Bow;
        else if (token == "cheer")
            out = EmoteGesture::Cheer;
        else if (token == "question")
            out = EmoteGesture::Question;
        else if (token == "exclamation")
            out = EmoteGesture::Exclamation;
        else
            return false;
        return true;
    }

    bool TryParseListenerGesture(std::string const& token, EmoteGesture& out)
    {
        if (token == "none")
            out = EmoteGesture::None;
        else if (token == "laugh")
            out = EmoteGesture::Laugh;
        else if (token == "applaud")
            out = EmoteGesture::Applaud;
        else if (token == "cheer")
            out = EmoteGesture::Cheer;
        else
            return false;
        return true;
    }

    char const* EmoteGestureToken(EmoteGesture gesture)
    {
        switch (gesture)
        {
            case EmoteGesture::Laugh: return "laugh";
            case EmoteGesture::Applaud: return "applaud";
            case EmoteGesture::Bow: return "bow";
            case EmoteGesture::Cheer: return "cheer";
            case EmoteGesture::Question: return "question";
            case EmoteGesture::Exclamation: return "exclamation";
            default: return "none";
        }
    }

    std::string BuildEffectsInstruction(EffectsOffer const& offer)
    {
        if (!offer.Any())
            return "";

        std::string s = "Trailer: [[AV_EFFECTS]] {";
        bool first = true;
        if (offer.offerSpeaker)
        {
            s += "\"speaker\":\"<laugh|applaud|bow|cheer|nod>\"";
            first = false;
        }
        if (offer.offerListener)
        {
            if (!first)
                s += ", ";
            s += "\"listener\":\"<laugh|applaud|cheer|nod>\"";
            first = false;
        }
        if (offer.offerRoleplay)
        {
            if (!first)
                s += ", ";
            s += "\"roleplay\":\"<action>\"";
            first = false;
        }
        s += "}. Omit if none.";
        return s;
    }

    uint32_t CalculateTokenHeadroom(EffectsOffer const& /*offer*/)
    {
        // 0 extra token overhead! The model budgets any optional trailer within its assigned dynamic tokens.
        return 0;
    }

    // Canonical Vanilla 1.12.1 / 1.18.1 mirror mapping table
    // Incoming TextEmote ID -> Bot response TextEmote ID
    static const std::unordered_map<uint32_t, uint32_t> s_mirrorEmoteMap = {
        // Greetings / Salutations
        {101, 101}, // WAVE -> WAVE
        {55,  101}, // HELLO -> WAVE
        {48,  101}, // GREET -> WAVE
        {19,  19},  // BYE -> BYE
        {102, 67},  // WELCOME -> NOD
        {17,  17},  // BOW -> BOW
        {78,  78},  // SALUTE -> SALUTE
        {33,  17},  // CURTSEY -> BOW
        {59,  67},  // KNEEL -> NOD
        // Affirmation / Gratitude
        {67,  67},  // NOD -> NOD
        {1,   67},  // AGREE -> NOD
        {66,  83},  // NO -> SHRUG
        {97,  67},  // THANK -> NOD
        {163, 163}, // SMILE -> SMILE
        // Celebrations & Encouragement
        {21,  21},  // CHEER -> CHEER
        {5,   5},   // APPLAUD -> APPLAUD
        {24,  24},  // CLAP -> CLAP
        {100, 21},  // VICTORY -> CHEER
        {26,  5},   // CONGRATULATE -> APPLAUD
        // Humor & Amusement
        {60,  60},  // LAUGH -> LAUGH
        {45,  45},  // GIGGLE -> GIGGLE
        {76,  60},  // ROFL -> LAUGH
        {329, 60},  // JOKE -> LAUGH
        // Affection & Playful
        {56,  56},  // HUG -> HUG
        {58,  12},  // KISS -> BLUSH
        {328, 84},  // FLIRT -> SHY
        // Provocation / Defiance
        {77,  22},  // RUDE -> CHICKEN
        {22,  77},  // CHICKEN -> RUDE
        {75,  75},  // ROAR -> ROAR
        {3,   77},  // ANGRY -> RUDE
        // Inquiry / Gestures
        {72,  67},  // POINT -> NOD
        {83,  83},  // SHRUG -> SHRUG
        {8,   83},  // BEG -> SHRUG
        {31,  83},  // CRY -> SHRUG
        {34,  34},  // DANCE -> DANCE
        {39,  21},  // FLEX -> CHEER
        {44,  83},  // GASP -> SHRUG
        {85,  83}   // SIGH -> SHRUG
    };

    static const std::unordered_set<uint32_t> s_contagiousEmotes = {
        34,  // TEXTEMOTE_DANCE
        21,  // TEXTEMOTE_CHEER
        60,  // TEXTEMOTE_LAUGH
        5,   // TEXTEMOTE_APPLAUD
        76,  // TEXTEMOTE_ROFL
        100  // TEXTEMOTE_VICTORY
    };

    static const std::unordered_set<uint32_t> s_ignoredSocialEmotes = {
        16,  // TEXTEMOTE_BRB
        165, // TEXTEMOTE_OOM
    };

    bool HasMirrorEmote(uint32_t textEmote, uint32_t& outMirrorEmote)
    {
        auto it = s_mirrorEmoteMap.find(textEmote);
        if (it != s_mirrorEmoteMap.end())
        {
            outMirrorEmote = it->second;
            return true;
        }
        return false;
    }

    bool IsContagiousEmote(uint32_t textEmote)
    {
        return s_contagiousEmotes.count(textEmote) > 0;
    }

    bool IsIgnoredSocialEmote(uint32_t textEmote)
    {
        return s_ignoredSocialEmotes.count(textEmote) > 0;
    }

    std::vector<uint32_t> GetSpectatorDuelEmotes(uint8_t moment, bool sideWon)
    {
        if (moment == 0) // Before duel
            return {21, 5, 78}; // CHEER, APPLAUD, SALUTE
        if (moment == 1) // During duel
            return {21, 5, 60, 44}; // CHEER, APPLAUD, LAUGH, GASP
        // moment 2: After duel
        if (sideWon)
            return {21, 60, 39, 75}; // CHEER, LAUGH, FLEX, ROAR
        return {3, 83, 85}; // ANGRY, SHRUG, SIGH
    }

    bool ValidateRoleplayBody(std::string const& raw, std::string& outCleaned, std::string const& speakerName)
    {
        if (!IsValidUtf8String(raw))
            return false;

        std::string s = TrimWhitespace(raw);
        if (s.empty() || s.size() > 120)
            return false;

        // Check for control characters (including \r, \n, \t, \0)
        for (unsigned char c : s)
        {
            if (c < 0x20 || c == 0x7F)
                return false;
        }

        // Reject command-like prefixes
        if (s.front() == '/' || s.front() == '.')
            return false;

        // Reject client markup, color, hyperlink sequences
        if (s.find("|c") != std::string::npos || s.find("|C") != std::string::npos ||
            s.find("|H") != std::string::npos || s.find("|h") != std::string::npos ||
            s.find("|r") != std::string::npos || s.find("|R") != std::string::npos)
            return false;

        // Reject reserved marker
        if (s.find("[[AV_EFFECTS]]") != std::string::npos)
            return false;

        // Strip or check leading duplicated speaker prefix
        if (!speakerName.empty())
        {
            if (s.compare(0, speakerName.size(), speakerName) == 0)
            {
                size_t remainder = speakerName.size();
                while (remainder < s.size() && (s[remainder] == ' ' || s[remainder] == ':'))
                    ++remainder;
                s = TrimWhitespace(s.substr(remainder));
                if (s.empty())
                    return false;
            }
        }

        outCleaned = s;
        return true;
    }

    bool ExtractAndStripEffects(std::string& inOutText, ParsedEffects& outEffects,
                                EffectsOffer const& offer, bool parserEnabled)
    {
        outEffects = ParsedEffects{};
        static std::string const marker = "[[AV_EFFECTS]]";

        size_t firstPos = inOutText.find(marker);
        if (firstPos == std::string::npos)
            return false;

        // Check for duplicate records
        size_t secondPos = inOutText.find(marker, firstPos + marker.size());
        if (secondPos != std::string::npos)
        {
            // Two or more marker lines: remove all of them from speech, accept no effects
            while (firstPos != std::string::npos)
            {
                size_t lineStart = inOutText.rfind('\n', firstPos);
                lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;
                size_t lineEnd = inOutText.find('\n', firstPos);
                if (lineEnd == std::string::npos)
                    inOutText.erase(lineStart);
                else
                    inOutText.erase(lineStart, lineEnd - lineStart + 1);
                firstPos = inOutText.find(marker);
            }
            inOutText = TrimWhitespace(inOutText);
            return false;
        }

        // Exactly one occurrence at firstPos
        size_t lineStart = inOutText.rfind('\n', firstPos);
        lineStart = (lineStart == std::string::npos) ? 0 : lineStart + 1;

        // Check whether there are non-whitespace characters preceding marker on the same line (inline record)
        bool hasPrecedingText = false;
        for (size_t i = lineStart; i < firstPos; ++i)
        {
            char c = inOutText[i];
            if (c != ' ' && c != '\t' && c != '\r')
            {
                hasPrecedingText = true;
                break;
            }
        }

        if (hasPrecedingText)
        {
            // Inline record: keep preceding prefix, strip marker suffix, accept no effects
            size_t lineEnd = inOutText.find('\n', firstPos);
            if (lineEnd == std::string::npos)
                inOutText.erase(firstPos);
            else
                inOutText.erase(firstPos, lineEnd - firstPos);
            inOutText = TrimWhitespace(inOutText);
            return false;
        }

        // Marker starts on this line. Find line end.
        size_t lineEnd = inOutText.find('\n', firstPos);
        size_t nextLineStart = (lineEnd == std::string::npos) ? inOutText.size() : lineEnd + 1;
        if (lineEnd == std::string::npos)
            lineEnd = inOutText.size();

        // Check whether subsequent lines have non-whitespace text (non-final record)
        bool hasSubsequentText = false;
        for (size_t i = nextLineStart; i < inOutText.size(); ++i)
        {
            char c = inOutText[i];
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            {
                hasSubsequentText = true;
                break;
            }
        }

        if (hasSubsequentText)
        {
            // Non-final record: strip marker line only, preserve surrounding speech, accept no effects
            inOutText.erase(lineStart, nextLineStart - lineStart);
            inOutText = TrimWhitespace(inOutText);
            return false;
        }

        // Candidate record is on the final physical line.
        // Check oversized bound: hard cap 512 bytes including marker
        size_t candidateBytes = lineEnd - lineStart;
        if (candidateBytes > 512)
        {
            // Oversized record: remove line, accept no effects
            inOutText.erase(lineStart);
            inOutText = TrimWhitespace(inOutText);
            return false;
        }

        // Extract JSON string following [[AV_EFFECTS]]
        size_t jsonStart = firstPos + marker.size();
        while (jsonStart < lineEnd && (inOutText[jsonStart] == ' ' || inOutText[jsonStart] == '\t'))
            ++jsonStart;

        std::string jsonStr = inOutText.substr(jsonStart, lineEnd - jsonStart);
        while (!jsonStr.empty() && (jsonStr.back() == '\r' || jsonStr.back() == ' ' || jsonStr.back() == '\t'))
            jsonStr.pop_back();

        // Strip the marker line from inOutText
        inOutText.erase(lineStart);
        inOutText = TrimWhitespace(inOutText);

        if (!parserEnabled || jsonStr.empty())
            return false;

        // Parse JSON strictly
        ParsedEffectsData data = ParseEffectsJson(jsonStr);

        if (!data.valid)
        {
            return false;
        }

        // Evaluate extracted fields against offer
        if (data.hasSpeaker)
        {
            if (offer.offerSpeaker)
            {
                EmoteGesture g = EmoteGesture::None;
                if (TryParseSpeakerGesture(data.speakerVal, g))
                {
                    outEffects.hasSpeaker = true;
                    outEffects.speaker = g;
                }
            }
        }

        if (data.hasListener)
        {
            if (offer.offerListener)
            {
                EmoteGesture g = EmoteGesture::None;
                if (TryParseListenerGesture(data.listenerVal, g))
                {
                    outEffects.hasListener = true;
                    outEffects.listener = g;
                }
            }
        }

        if (data.hasRoleplay)
        {
            if (offer.offerRoleplay)
            {
                std::string cleaned;
                if (ValidateRoleplayBody(data.roleplayVal, cleaned))
                {
                    outEffects.hasRoleplay = true;
                    outEffects.roleplay = cleaned;
                }
            }
        }

        return outEffects.Any();
    }
}
