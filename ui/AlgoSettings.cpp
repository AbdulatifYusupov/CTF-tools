#include "AlgoSettings.hpp"

#include <charconv>
#include <stdexcept>

namespace app {
namespace {

int parseInt(const std::string& key, const std::string& value) {
    int out = 0;
    const char* b = value.data();
    const char* e = b + value.size();
    const auto [p, ec] = std::from_chars(b, e, out);
    if (ec != std::errc{} || p != e) throw std::invalid_argument("profile parameter '" + key + "' is not an integer: " + value);
    return out;
}

}  // namespace

std::string validate(const AlgoSettings& s) {
    if (s.algo == "adaptive-lsb") {
        if (s.bits < 1 || s.bits > 2) return "bits must be 1 or 2";
        if (s.threshold < 0) return "threshold must be >= 0";
    } else if (s.algo == "block-dct") {
        if (s.pairs < 1 || s.pairs > 3) return "pairs per block must be 1..3";
        if (s.strength < 8 || s.strength > 100) return "strength must be 8..100";
        if (s.parity < 2 || s.parity > 100) return "parity symbols must be 2..100";
    } else {
        return "unknown algorithm: " + s.algo;
    }
    return {};
}

cli::ProfileMap toProfile(const AlgoSettings& s) {
    cli::ProfileMap p;
    p["algo"] = s.algo;
    p["mode"] = s.mode == Mode::Kem ? "kem" : "password";
    if (s.algo == "block-dct") {
        p["pairs"]  = std::to_string(s.pairs);
        p["parity"] = std::to_string(s.parity);
    } else {
        p["bits"]      = std::to_string(s.bits);
        p["threshold"] = std::to_string(s.threshold);
    }
    return p;
}

AlgoSettings fromProfile(const cli::ProfileMap& profile, AlgoSettings base) {
    const auto algo = profile.find("algo");
    if (algo == profile.end()) throw std::invalid_argument("profile has no algorithm");
    if (algo->second == "audio-lsb") throw std::invalid_argument("audio-lsb profiles are not supported by the GUI (images only)");
    base.algo = algo->second;

    for (const auto& [k, v] : profile) {
        if (k == "algo") continue;
        if (k == "mode") {
            if (v == "kem") base.mode = Mode::Kem;
            else if (v == "password") base.mode = Mode::Password;
            else throw std::invalid_argument("bad mode in profile: " + v);
        } else if (k == "bits") base.bits = parseInt(k, v);
        else if (k == "threshold") base.threshold = parseInt(k, v);
        else if (k == "pairs") base.pairs = parseInt(k, v);
        else if (k == "parity") base.parity = parseInt(k, v);
        else throw std::invalid_argument("unexpected profile key: " + k);
    }
    if (const std::string err = validate(base); !err.empty()) throw std::invalid_argument(err);
    return base;
}

}  // namespace app
