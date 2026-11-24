#include "Profile.hpp"

#include <cctype>
#include <climits>
#include <set>
#include <stdexcept>

namespace cli {

namespace {

enum class ValueType {
    String,
    Number,
};

struct JsonValue {
    ValueType type;
    std::string strVal;
    int numVal = 0;
};

}  // namespace

std::string profileToJson(const ProfileMap& profile) {
    std::string out = "{\n  \"format\": \"stego-profile-1\"";
    if (profile.count("algo")) {
        out += ",\n  \"algo\": \"" + profile.at("algo") + "\"";
    }
    if (profile.count("mode")) {
        out += ",\n  \"mode\": \"" + profile.at("mode") + "\"";
    }
    for (const auto& [k, v] : profile) {
        if (k == "algo" || k == "mode" || k == "format") continue;
        out += ",\n  \"" + k + "\": " + v;
    }
    out += "\n}\n";
    return out;
}

ProfileMap parseProfileJson(const std::string& json) {
    size_t i = 0;
    const size_t n = json.size();

    auto skipWs = [&]() {
        while (i < n && (json[i] == ' ' || json[i] == '\t' || json[i] == '\r' || json[i] == '\n')) {
            ++i;
        }
    };

    auto parseString = [&](std::string& out) {
        if (i >= n || json[i] != '"') return false;
        ++i;
        out.clear();
        while (i < n) {
            char c = json[i++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20) {
                return false;
            }
            if (c == '\\') {
                if (i >= n) return false;
                char esc = json[i++];
                if (esc == 'u' || esc == 'U') {
                    return false;
                }
                if (esc == '"' || esc == '\\' || esc == '/') {
                    out += esc;
                } else if (esc == 'b') {
                    out += '\b';
                } else if (esc == 'f') {
                    out += '\f';
                } else if (esc == 'n') {
                    out += '\n';
                } else if (esc == 'r') {
                    out += '\r';
                } else if (esc == 't') {
                    out += '\t';
                } else {
                    return false;
                }
            } else {
                out += c;
            }
        }
        return false;
    };

    skipWs();
    if (i >= n || json[i] != '{') {
        throw UsageError("profile JSON must be an object");
    }
    ++i;
    skipWs();

    std::map<std::string, JsonValue> entries;
    std::set<std::string> seenKeys;

    bool first = true;
    while (i < n && json[i] != '}') {
        if (!first) {
            if (json[i] != ',') {
                throw UsageError("expected ',' between object entries");
            }
            ++i;
            skipWs();
            if (i < n && json[i] == '}') {
                throw UsageError("trailing comma not allowed in profile JSON");
            }
        }
        first = false;

        std::string key;
        if (!parseString(key)) {
            throw UsageError("expected string key in profile JSON");
        }
        if (seenKeys.count(key)) {
            throw UsageError("duplicate key in profile JSON: " + key);
        }
        seenKeys.insert(key);

        skipWs();
        if (i >= n || json[i] != ':') {
            throw UsageError("expected ':' after key in profile JSON");
        }
        ++i;
        skipWs();

        JsonValue val;
        if (i < n && json[i] == '"') {
            val.type = ValueType::String;
            if (!parseString(val.strVal)) {
                throw UsageError("invalid string value in profile JSON");
            }
        } else if (i < n && (json[i] == '-' || std::isdigit(static_cast<unsigned char>(json[i])))) {
            val.type = ValueType::Number;
            size_t start = i;
            if (json[i] == '-') {
                ++i;
                if (i >= n || !std::isdigit(static_cast<unsigned char>(json[i]))) {
                    throw UsageError("invalid number in profile JSON");
                }
            }
            size_t digits = 0;
            while (i < n && std::isdigit(static_cast<unsigned char>(json[i]))) {
                ++i;
                ++digits;
            }
            if (digits > 10) {
                throw UsageError("number too large in profile JSON");
            }
            if (i < n && (json[i] == '.' || json[i] == 'e' || json[i] == 'E' ||
                          std::isalnum(static_cast<unsigned char>(json[i])))) {
                throw UsageError("invalid integer value in profile JSON");
            }
            std::string numStr = json.substr(start, i - start);
            try {
                long long num = std::stoll(numStr);
                if (num > 2147483647LL || num < -2147483648LL) {
                    throw UsageError("integer out of range in profile JSON");
                }
                val.numVal = static_cast<int>(num);
                val.strVal = std::to_string(val.numVal);
            } catch (...) {
                throw UsageError("invalid integer in profile JSON");
            }
        } else {
            throw UsageError("unsupported value type in profile JSON");
        }

        entries[key] = val;
        skipWs();
    }

    if (i >= n || json[i] != '}') {
        throw UsageError("expected '}' at end of profile JSON");
    }
    ++i;
    skipWs();
    if (i < n) {
        throw UsageError("trailing garbage after profile JSON object");
    }

    // Semantic validation
    if (!entries.count("format")) {
        throw UsageError("profile JSON missing 'format'");
    }
    if (entries["format"].type != ValueType::String || entries["format"].strVal != "stego-profile-1") {
        throw UsageError("unsupported profile format");
    }

    if (!entries.count("algo")) {
        throw UsageError("profile JSON missing 'algo'");
    }
    if (entries["algo"].type != ValueType::String) {
        throw UsageError("profile 'algo' must be a string");
    }
    const std::string& algo = entries["algo"].strVal;
    if (algo != "adaptive-lsb" && algo != "audio-lsb" && algo != "block-dct") {
        throw UsageError("unknown algorithm in profile: " + algo);
    }

    if (entries.count("mode")) {
        if (entries["mode"].type != ValueType::String) {
            throw UsageError("profile 'mode' must be a string");
        }
        const std::string& mode = entries["mode"].strVal;
        if (mode != "kem" && mode != "password") {
            throw UsageError("bad mode in profile: " + mode);
        }
    }

    // Typo guard and parameter check
    for (const auto& [k, v] : entries) {
        if (k == "format" || k == "algo" || k == "mode") continue;

        if (algo == "block-dct") {
            if (k != "pairs" && k != "parity") {
                throw UsageError("unexpected key for block-dct profile: " + k);
            }
        } else {
            if (k != "bits" && k != "threshold") {
                throw UsageError("unexpected key for " + algo + " profile: " + k);
            }
        }

        if (v.type != ValueType::Number) {
            throw UsageError("parameter '" + k + "' must be an integer");
        }
    }

    ProfileMap result;
    result["algo"] = algo;
    if (entries.count("mode")) {
        result["mode"] = entries["mode"].strVal;
    }
    for (const auto& [k, v] : entries) {
        if (k != "format" && k != "algo" && k != "mode") {
            result[k] = v.strVal;
        }
    }
    return result;
}

void applyProfile(Args& args, const ProfileMap& profile) {
    if (profile.empty()) return;

    if (profile.count("algo")) {
        const std::string& profAlgo = profile.at("algo");
        if (args.has("algo") && args.get("algo") != profAlgo) {
            throw UsageError("option --algo contradicts profile algorithm " + profAlgo);
        }
        if (profAlgo == "block-dct") {
            if (args.has("bits") || args.has("threshold")) {
                throw UsageError("option contradicts profile algorithm block-dct");
            }
        } else if (profAlgo == "adaptive-lsb" || profAlgo == "audio-lsb") {
            if (args.has("pairs") || args.has("parity") || args.has("strength")) {
                throw UsageError("option contradicts profile algorithm " + profAlgo);
            }
        }
    }

    if (profile.count("mode")) {
        const std::string& mode = profile.at("mode");
        args.set("profile-mode", mode);
        if (args.command == "capacity") {
            if (!args.has("mode")) {
                args.set("mode", mode);
            }
        }
    }

    for (const auto& [k, v] : profile) {
        if (k == "mode") continue;
        if (args.has(k)) {
            if (args.get(k) != v) {
                throw UsageError("option --" + k + " contradicts profile");
            }
        } else {
            args.set(k, v);
        }
    }
}

}  // namespace cli
