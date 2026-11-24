#pragma once
#include "Args.hpp"

#include <map>
#include <string>

namespace cli {

using ProfileMap = std::map<std::string, std::string>;

// Serializes a ProfileMap to formatted JSON conforming to "stego-profile-1".
std::string profileToJson(const ProfileMap& profile);

// Parses and validates a JSON profile string.
// Throws UsageError on malformed JSON, format mismatch, or invalid parameters.
ProfileMap parseProfileJson(const std::string& json);

// Merges profile settings into the parsed CLI arguments.
// Throws UsageError if explicit CLI arguments contradict the profile.
void applyProfile(Args& args, const ProfileMap& profile);

}  // namespace cli
