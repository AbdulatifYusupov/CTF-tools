#include "Args.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

using V = std::vector<std::string>;

static bool usageError(const V& argv) {
    try { (void)cli::parseArgs(argv); }
    catch (const cli::UsageError&) { return true; }
    return false;
}

int main() {
    // happy paths
    CHECK(cli::parseArgs({}).command == "help");
    CHECK(cli::parseArgs({"--help"}).command == "help");
    CHECK(cli::parseArgs({"-h"}).command == "help");
    {
        const auto a = cli::parseArgs({"embed", "--algo", "block-dct", "--in", "a.png", "--out=b.png",
                                       "--msg", "hello world", "--progress", "--pairs", "3"});
        CHECK(a.command == "embed");
        CHECK(a.get("algo") == "block-dct" && a.get("out") == "b.png" && a.get("msg") == "hello world");
        CHECK(a.flag("progress") && !a.flag("force"));
        CHECK(a.has("pairs") && a.getInt("pairs", 2) == 3);
        CHECK(a.getInt("parity", 32) == 32);          // absent -> fallback
        CHECK(a.get("pub", "x") == "x");
    }
    // a value that starts with "--" must be possible via --name=value
    CHECK(cli::parseArgs({"embed", "--msg=--looks like an option"}).get("msg") == "--looks like an option");
    // a lone "-" is a value (stdin/stdout), not an option
    CHECK(cli::parseArgs({"embed", "--file", "-"}).get("file") == "-");
    // empty inline value is allowed and distinct from missing
    CHECK(cli::parseArgs({"extract", "--out="}).has("out"));

    // errors
    CHECK(usageError({"frobnicate"}));                               // unknown command
    CHECK(usageError({"embed", "--nope", "1"}));                     // unknown option
    CHECK(usageError({"extract", "--strength", "12"}));              // encoder-only option rejected for extract
    CHECK(usageError({"embed", "--algo"}));                          // missing value
    CHECK(usageError({"embed", "--algo", "--in", "x"}));             // next token is an option
    CHECK(usageError({"embed", "--algo", "a", "--algo", "b"}));      // duplicate
    CHECK(usageError({"embed", "--progress=yes"}));                  // flag with value
    CHECK(usageError({"embed", "stray"}));                           // positional argument
    CHECK(usageError({"embed", "--"}));                              // bare "--"
    CHECK(usageError({"keygen", "--progress"}));                     // option of another command

    // password-mode options
    {
        const auto a = cli::parseArgs({"embed", "--password-file", "pw.txt", "--ask-password", "--algo", "block-dct"});
        CHECK(a.get("password-file") == "pw.txt" && a.flag("ask-password") && !a.has("pub"));
        CHECK(cli::parseArgs({"extract", "--password", "x"}).get("password") == "x");
        CHECK(cli::parseArgs({"capacity", "--mode", "password"}).get("mode") == "password");
    }
    CHECK(usageError({"embed", "--ask-password=yes"}));              // flag with value
    CHECK(usageError({"keygen", "--password-file", "x"}));           // option of another command
    CHECK(usageError({"analyze", "--mode", "kem"}));

    // helpers
    {
        const auto a = cli::parseArgs({"capacity", "--bits", "two", "--threshold", "12x", "--in", "f"});
        bool t1 = false, t2 = false, t3 = false;
        try { (void)a.getInt("bits", 1); } catch (const cli::UsageError&) { t1 = true; }
        try { (void)a.getInt("threshold", 1); } catch (const cli::UsageError&) { t2 = true; }  // trailing junk
        try { (void)a.require("algo"); } catch (const cli::UsageError&) { t3 = true; }
        CHECK(t1 && t2 && t3);
        CHECK(a.require("in") == "f");
    }

    std::puts("cli_args_test: OK");
    return 0;
}
