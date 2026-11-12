#pragma once

#include "AzerothVoicesTypes.h"
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace AzerothVoices
{
    /**
     * Map string tokens to EmoteGesture constants.
     * Case-sensitive lower-case enum matching.
     */
    bool TryParseSpeakerGesture(std::string const& token, EmoteGesture& out);
    bool TryParseListenerGesture(std::string const& token, EmoteGesture& out);
    char const* EmoteGestureToken(EmoteGesture gesture);

    /**
     * Build the [[AV_EFFECTS]] system prompt instruction for the offered fields.
     */
    std::string BuildEffectsInstruction(EffectsOffer const& offer);

    /**
     * Calculate token headroom needed for the offered effects trailer.
     */
    uint32_t CalculateTokenHeadroom(EffectsOffer const& offer);

    /**
     * Strict validation of third-person roleplay action body.
     */
    bool ValidateRoleplayBody(std::string const& raw, std::string& outCleaned, std::string const& speakerName = "");

    /**
     * Pure extraction and stripping of the [[AV_EFFECTS]] trailer from model reply.
     * Operates before speech sanitation, stripping control markers from dialogue text.
     */
    bool ExtractAndStripEffects(std::string& inOutText, ParsedEffects& outEffects,
                                EffectsOffer const& offer, bool parserEnabled = true);

    /**
     * Interactive 0-Token Emote Perception & Mirroring helpers.
     */
    bool HasMirrorEmote(uint32_t textEmote, uint32_t& outMirrorEmote);
    bool IsContagiousEmote(uint32_t textEmote);
    bool IsIgnoredSocialEmote(uint32_t textEmote);
    std::vector<uint32_t> GetSpectatorDuelEmotes(uint8_t moment, bool sideWon);

    /**
     * Angle math for conversation facing.
     */
    inline float NormalizeOrientation(float ori)
    {
        float const twoPi = 6.28318530717958647692f;
        ori = std::fmod(ori, twoPi);
        if (ori < 0.0f)
            ori += twoPi;
        return ori;
    }

    inline float ShortestAngleDifference(float angle1, float angle2)
    {
        float const twoPi = 6.28318530717958647692f;
        float const pi = 3.14159265358979323846f;
        float diff = std::fabs(NormalizeOrientation(angle1) - NormalizeOrientation(angle2));
        if (diff > pi)
            diff = twoPi - diff;
        return diff;
    }

    inline bool AnglesWithinTolerance(float angle1, float angle2, float tolerance = 0.05f)
    {
        return ShortestAngleDifference(angle1, angle2) <= tolerance;
    }
}
