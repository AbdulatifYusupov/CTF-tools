// Headless test of the GUI's settings <-> profile logic (no Qt, no OpenCV).
#include "AlgoSettings.hpp"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

template <class F>
bool throwsInvalid(F f) {
    try { f(); } catch (const std::invalid_argument&) { return true; } catch (...) { return false; }
    return false;
}

int main() {
    using namespace app;

    // defaults are valid
    CHECK(validate(AlgoSettings{}).empty());

    // ranges mirror the algorithm constructors
    { AlgoSettings s; s.bits = 3; CHECK(!validate(s).empty()); }
    { AlgoSettings s; s.threshold = -1; CHECK(!validate(s).empty()); }
    { AlgoSettings s; s.algo = "block-dct"; s.pairs = 4; CHECK(!validate(s).empty()); }
    { AlgoSettings s; s.algo = "block-dct"; s.strength = 3; CHECK(!validate(s).empty()); }
    { AlgoSettings s; s.algo = "block-dct"; s.strength = 4; CHECK(validate(s).empty()); }
    { AlgoSettings s; s.algo = "block-dct"; CHECK(s.strength == 8); }  // measured default
    { AlgoSettings s; s.algo = "block-dct"; s.parity = 1; CHECK(!validate(s).empty()); }
    { AlgoSettings s; s.algo = "audio-lsb"; CHECK(!validate(s).empty()); }
    // bits is irrelevant for block-dct, so a bad value there must not matter
    { AlgoSettings s; s.algo = "block-dct"; s.bits = 99; CHECK(validate(s).empty()); }

    // round trip through the real JSON writer/parser, both algorithms and both modes
    for (const char* algo : {"adaptive-lsb", "block-dct"})
        for (Mode m : {Mode::Password, Mode::Kem}) {
            AlgoSettings s;
            s.algo = algo; s.mode = m; s.bits = 2; s.threshold = 17; s.pairs = 3; s.parity = 40; s.strength = 55;
            const std::string json = cli::profileToJson(toProfile(s));
            AlgoSettings base;
            base.strength = 77;  // must survive: strength is not part of the profile
            const AlgoSettings r = fromProfile(cli::parseProfileJson(json), base);
            CHECK(r.algo == s.algo && r.mode == s.mode);
            CHECK(r.strength == 77);
            if (s.algo == "block-dct") CHECK(r.pairs == 3 && r.parity == 40);
            else CHECK(r.bits == 2 && r.threshold == 17);
        }

    // the profile never carries strength (it is encoder-only)
    { AlgoSettings s; s.algo = "block-dct"; CHECK(toProfile(s).count("strength") == 0); }

    // rejections
    CHECK(throwsInvalid([] { fromProfile({{"algo", "audio-lsb"}, {"bits", "1"}}, AlgoSettings{}); }));
    CHECK(throwsInvalid([] { fromProfile({}, AlgoSettings{}); }));
    CHECK(throwsInvalid([] { fromProfile({{"algo", "adaptive-lsb"}, {"bits", "3"}}, AlgoSettings{}); }));
    CHECK(throwsInvalid([] { fromProfile({{"algo", "adaptive-lsb"}, {"bits", "x"}}, AlgoSettings{}); }));
    CHECK(throwsInvalid([] { fromProfile({{"algo", "adaptive-lsb"}, {"mode", "rot13"}}, AlgoSettings{}); }));
    CHECK(throwsInvalid([] { fromProfile({{"algo", "adaptive-lsb"}, {"strength", "12"}}, AlgoSettings{}); }));

    std::puts("algo_settings_test: OK");
    return 0;
}
