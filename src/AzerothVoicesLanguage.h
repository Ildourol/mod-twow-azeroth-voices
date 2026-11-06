#pragma once
#include <cstdint>
#include <map>
#include <string>
namespace AzerothVoices { namespace Language {
std::string Normalize(std::string value);
bool Valid(std::string const& code);
std::string Name(std::string const& code);
bool ContainsGreek(std::string const& text);
bool ResponseValid(std::string const& text, std::string const& secondary, bool classify, std::string const& actor = "", std::string const& speaker = "");
char Extract(std::string& text, bool secondary);
std::string Instruction(std::string const& secondary, std::string const& previous, bool classify, bool preserve);
} }
