#pragma once
// GUI-side algorithm settings: validation and conversion to/from the CLI profile format
// ("stego-profile-1"). No Qt and no OpenCV here, so it is unit-tested headless.
#include "Profile.hpp"

#include <string>

namespace app {

enum class Mode { Password, Kem };

struct AlgoSettings {
    std::string algo = "adaptive-lsb";  // "adaptive-lsb" | "block-dct" (images only)
    int bits         = 1;               // adaptive-lsb
    int threshold    = 32;              // adaptive-lsb
    int pairs        = 2;               // block-dct
    int parity       = 32;              // block-dct
    int strength     = 12;              // block-dct, encoder only: NOT part of the profile
    Mode mode        = Mode::Password;
};

// Empty string = valid, otherwise a human-readable reason. Same ranges as the algorithm constructors.
std::string validate(const AlgoSettings& s);

// What must be identical at embed and extract time (+ "mode"). Never contains secrets or strength.
cli::ProfileMap toProfile(const AlgoSettings& s);

// Applies a parsed profile on top of `base` (strength and anything the profile does not mention
// are kept). Throws std::invalid_argument for audio-lsb, unknown keys, bad numbers, bad ranges.
AlgoSettings fromProfile(const cli::ProfileMap& profile, AlgoSettings base);

}  // namespace app
