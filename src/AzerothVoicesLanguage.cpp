#include "AzerothVoicesLanguage.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <vector>
#include <initializer_list>

namespace AzerothVoices { namespace Language {

static std::map<std::string,std::string> const names = {
    {"GR","Greeklish"},
    {"DE","German"},
    {"FR","French"},
    {"ES","Spanish"},
    {"IT","Italian"},
    {"PT","Portuguese"},
    {"PL","Polish"},
    {"RU","Russian"},
    {"TR","Turkish"},
    {"NL","Dutch"}
};

std::string Normalize(std::string value) {
    std::string out;
    for (unsigned char c : value)
        if (!std::isspace(c) && c != '-')
            out += static_cast<char>(std::toupper(c));
    return out;
}

bool Valid(std::string const& code) {
    return names.count(code) != 0;
}

std::string Name(std::string const& code) {
    auto i = names.find(code);
    return i == names.end() ? "English" : i->second;
}

char Extract(std::string& text, bool secondary) {
    char decision = 0;
    bool invalid = false;

    // 1. Match canonical [[AV_LANG:X]]
    std::string const fullPrefix = "[[AV_LANG:";
    size_t pos = 0;
    while ((pos = text.find(fullPrefix)) != std::string::npos) {
        auto end = text.find("]]", pos + fullPrefix.size());
        if (end == std::string::npos) {
            text.erase(pos);
            invalid = true;
            break;
        }
        auto v = text.substr(pos + fullPrefix.size(), end - pos - fullPrefix.size());
        auto first = v.find_first_not_of(" \t\r\n");
        auto last = v.find_last_not_of(" \t\r\n");
        char c = (first != std::string::npos && first == last) ? v[first] : 0;
        if ((c != 'E' && c != 'S' && c != 'U' && c != 'A') || (c == 'S' && !secondary) || (decision && decision != c))
            invalid = true;
        else
            decision = c;
        text.erase(pos, end + 2 - pos);
    }

    // 2. Also match shortened [[X]] tags (e.g. [[E]], [[S]], [[U]], [[A]]) that LLMs sometimes generate
    static char const* const shortTags[] = { "[[E]]", "[[S]]", "[[U]]", "[[A]]" };
    for (char const* tagStr : shortTags) {
        std::string tag = tagStr;
        while ((pos = text.find(tag)) != std::string::npos) {
            char c = tag[2]; // 'E', 'S', 'U', or 'A'
            if ((c == 'S' && !secondary) || (decision && decision != c))
                invalid = true;
            else if (!decision)
                decision = c;
            text.erase(pos, tag.size());
        }
    }

    // Trim any leading whitespace left over from erasing the prefix tag
    auto firstNonSpace = text.find_first_not_of(" \t\r\n");
    if (firstNonSpace != std::string::npos && firstNonSpace > 0)
        text.erase(0, firstNonSpace);

    return invalid ? 0 : decision;
}

bool ContainsGreek(std::string const& text) {
    for (size_t i = 0; i < text.size(); ++i) {
        // Functional links are opaque; their display names may contain Greek.
        if (text.compare(i, 2, "|H") == 0) {
            auto end = text.find("|h", i + 2);
            if (end != std::string::npos) end = text.find("|h", end + 2);
            if (end != std::string::npos) { i = end + 1; continue; }
        }
        unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c == 0xCD || c == 0xCE || c == 0xCF) && i + 1 < text.size()) {
            unsigned char d = static_cast<unsigned char>(text[i+1]);
            if (d >= 0x80 && d <= 0xBF && (c != 0xCD || d >= 0xB0)) return true;
        }
        if (c == 0xE1 && i + 2 < text.size()) {
            unsigned char d = static_cast<unsigned char>(text[i+1]);
            if (d >= 0xBC && d <= 0xBF) return true;
        }
    }
    return false;
}

bool ResponseValid(std::string const& text, std::string const& secondary, bool classify, std::string const& actor, std::string const& speaker) {
    std::string clean = text;
    char decision = Extract(clean, !secondary.empty());
    if (classify && !decision) return false;
    if (clean.find_first_not_of(" \t\r\n") == std::string::npos) return false;
    for (auto const& name : {actor, speaker}) {
        if (name.empty()) continue;
        size_t pos = 0;
        while ((pos = clean.find(name, pos)) != std::string::npos) clean.erase(pos, name.size());
    }
    return secondary != "GR" || !ContainsGreek(clean);
}

std::string Instruction(std::string const& secondary, std::string const& previous, bool classify, bool preserve) {
    std::string s;
    if (!classify) {
        s = "Reply in " + Name(previous) + ".";
    } else {
        s = "LANGUAGE UNDERSTANDING RULES:\n"
            "- You understand English" + (secondary.empty() ? "" : " and " + Name(secondary)) + ".\n"
            "- Match supported input; honor explicit requests to reply in a supported language.\n"
            "- Otherwise, if the player speaks in English, answer in English.\n"
            "- If the player asks in English about your language capabilities (e.g., 'do you speak Greeklish?', 'which language do you know?'), answer their question in English.\n";

        if (!secondary.empty()) {
            s += "- Otherwise, if the player speaks in " + Name(secondary) + ", reply naturally in " + Name(secondary) + ".\n";
        }

        s += "- If the input is in an unsupported foreign language that is neither English" +
             (secondary.empty() ? "" : " nor " + Name(secondary)) +
             ", politely express that you do not understand or only speak English" +
             (secondary.empty() ? "" : "/" + Name(secondary)) + " in your character's natural tone.\n"
             "- Ignore names, links, quoted text and isolated game terms when classifying.\n"
            "- For ambiguous text, continue in " + Name(previous) + ".\n"
             "- Hidden Classification Prefix: Begin your reply with the exact tag [[AV_LANG:E]] for English, "
             "[[AV_LANG:S]] for " + (secondary.empty() ? "secondary" : Name(secondary)) +
             ", [[AV_LANG:U]] for unsupported languages, or [[AV_LANG:A]] for ambiguous text. Do not omit this tag.";
    }

    if (secondary == "GR" && (classify || previous == "GR")) {
        s += "\nGREEKLISH GUIDELINES: Both Greek alphabet script and Latin-alphabet Greeklish (e.g. 'ti kaneis', 'ola kala', 'ti nea', 'geia sou', 'kalimera', 'pame') are Greek. Write only the conversational Greek prose in Latin-character Greeklish. This is not an instruction to translate or transliterate protected English game terms.";
    }

    if (preserve) {
        s += " TERMINOLOGY EXCEPTION overrides the output-language rule: copy supplied English gameplay words, abbreviations, class/race labels and item, spell, quest, dungeon, zone and boss names exactly. Translate only surrounding conversational prose. Keep ordinary gameplay terms such as quest, tank, healer, DPS, raid, guild and loot in English too; do not translate them, phoneticize them or attach target-language endings. For example, keep quest instead of Greeklish apostoli and tank instead of Greeklish dexameni when referring to gameplay. If unsure of a game name, reuse the supplied name instead of inventing a translation. Rephrase the sentence around protected terms.";
    }
    s += " Unsupported or ambiguous language alone must not decrease sentiment. Preserve functional item links, player names and commands.";
    std::replace(s.begin(), s.end(), '\n', ' ');
    return s;
}

} }
