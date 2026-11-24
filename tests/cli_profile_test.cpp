#include "Profile.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

using cli::ProfileMap;

static bool rejects(const std::string& json) {
    try { (void)cli::parseProfileJson(json); }
    catch (const cli::UsageError&) { return true; }
    return false;
}

static const char* kHead = R"("format": "stego-profile-1")";

static std::string withHead(const std::string& rest) { return std::string("{") + kHead + (rest.empty() ? "" : ", " + rest) + "}"; }

int main() {
    // ---- writer output is stable and parses back to the same map ---------------------------------
    {
        const ProfileMap p = {{"algo", "block-dct"}, {"mode", "password"}, {"pairs", "2"}, {"parity", "32"}};
        const std::string json = cli::profileToJson(p);
        CHECK(json ==
              "{\n  \"format\": \"stego-profile-1\",\n  \"algo\": \"block-dct\",\n  \"mode\": \"password\",\n"
              "  \"pairs\": 2,\n  \"parity\": 32\n}\n");
        CHECK(cli::parseProfileJson(json) == p);

        const ProfileMap q = {{"algo", "adaptive-lsb"}, {"mode", "kem"}, {"bits", "2"}, {"threshold", "24"}};
        CHECK(cli::parseProfileJson(cli::profileToJson(q)) == q);
    }

    // ---- parser accepts what a human may write ----------------------------------------------------
    {
        const auto p = cli::parseProfileJson(" \n\t{ \"algo\" :\"audio-lsb\" ,\r\n \"bits\":1, \"threshold\": 64 ,"
                                             " \"format\":\"stego-profile-1\" }\n ");
        CHECK(p.size() == 3 && p.at("algo") == "audio-lsb" && p.at("bits") == "1" && p.at("threshold") == "64");
        CHECK(!p.count("mode"));  // mode is optional
    }

    // ---- strictness: every one of these must be refused --------------------------------------------
    CHECK(rejects(""));                                                    // empty
    CHECK(rejects("[]"));                                                  // not an object
    CHECK(rejects("{"));                                                   // truncated
    CHECK(rejects(withHead(R"("algo": "block-dct",)")));                   // trailing comma
    CHECK(rejects(withHead(R"("algo": "block-dct")") + " x"));             // trailing garbage
    CHECK(rejects(R"({"algo": "block-dct"})"));                            // no format
    CHECK(rejects(R"({"format": "stego-profile-2", "algo": "block-dct"})"));  // wrong format version
    CHECK(rejects(withHead("")));                                          // no algo
    CHECK(rejects(withHead(R"("algo": "rot13")")));                        // unknown algorithm
    CHECK(rejects(withHead(R"("algo": "block-dct", "algo": "block-dct")")));  // duplicate key
    CHECK(rejects(withHead(R"("algo": "block-dct", "color": "red")")));    // unknown key (typo guard)
    CHECK(rejects(withHead(R"("algo": "block-dct", "pairs": "2")")));      // number given as string
    CHECK(rejects(withHead(R"("algo": 5)")));                              // string given as number
    CHECK(rejects(withHead(R"("algo": "block-dct", "pairs": 2.5)")));      // not an integer
    CHECK(rejects(withHead(R"("algo": "block-dct", "pairs": 1e3)")));      // exponent
    CHECK(rejects(withHead(R"("algo": "block-dct", "pairs": true)")));     // booleans unsupported
    CHECK(rejects(withHead(R"("algo": "block-dct", "pairs": {"a":1})")));  // nested value
    CHECK(rejects(withHead(R"("algo": "block-dct", "pairs": 1234567890123)")));  // absurdly long number
    CHECK(rejects(withHead(R"("algo": "block-dct", "bits": 1)")));         // parameter of another algorithm
    CHECK(rejects(withHead(R"("algo": "adaptive-lsb", "pairs": 2)")));     // ... and the other way round
    CHECK(rejects(withHead(R"("algo": "block-dct", "mode": "both")")));    // bad mode
    CHECK(rejects(withHead(R"("algo": "block-\u0064ct")")));               // \u escapes are not supported
    CHECK(rejects(withHead(R"("algo": "block-dct)")));                     // unterminated string

    // ---- applyProfile: fills gaps, refuses contradictions ------------------------------------------
    {
        const ProfileMap p = {{"algo", "block-dct"}, {"mode", "password"}, {"pairs", "3"}, {"parity", "24"}};

        auto a = cli::parseArgs({"extract", "--in", "x.png", "--password-file", "pw.txt"});
        cli::applyProfile(a, p);
        CHECK(a.get("algo") == "block-dct" && a.getInt("pairs", 0) == 3 && a.getInt("parity", 0) == 24);
        CHECK(a.get("profile-mode") == "password");
        CHECK(!a.has("mode"));  // only `capacity` takes --mode from the profile

        // an explicit option that AGREES is fine
        auto same = cli::parseArgs({"extract", "--algo", "block-dct", "--pairs", "3"});
        cli::applyProfile(same, p);
        CHECK(same.getInt("parity", 0) == 24);

        // an explicit option that CONTRADICTS is an error, never a silent override
        auto clash = cli::parseArgs({"extract", "--pairs", "2"});
        bool threw = false;
        try { cli::applyProfile(clash, p); } catch (const cli::UsageError&) { threw = true; }
        CHECK(threw);

        auto wrongAlgo = cli::parseArgs({"extract", "--algo", "adaptive-lsb"});
        threw = false;
        try { cli::applyProfile(wrongAlgo, p); } catch (const cli::UsageError&) { threw = true; }
        CHECK(threw);

        // `capacity` takes the mode as its default, but an explicit --mode wins only if equal
        auto cap = cli::parseArgs({"capacity", "--in", "x.png"});
        cli::applyProfile(cap, p);
        CHECK(cap.get("mode") == "password");
        auto capKem = cli::parseArgs({"capacity", "--in", "x.png", "--mode", "kem"});
        cli::applyProfile(capKem, p);  // explicit --mode kem is kept (mode is a hint, not a parameter)
        CHECK(capKem.get("mode") == "kem");
    }

    std::puts("cli_profile_test: OK");
    return 0;
}
