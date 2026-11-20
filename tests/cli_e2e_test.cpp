// Drives the real stego_cli binary: keygen -> embed -> extract for each algorithm, plus the
// failure paths and their exit codes. Usage: cli_e2e_test <path-to-stego_cli>
#include "stego/AudioIO.hpp"

#include <opencv2/imgcodecs.hpp>

#include <sndfile.h>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

namespace fs = std::filesystem;

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

#ifdef _WIN32
int main() {
    std::puts("cli_e2e_test: skipped on Windows");
    return 0;
}
#else

static std::string g_cli, g_dir;

static std::string q(const std::string& s) { return "'" + s + "'"; }  // POSIX single quoting (paths are ours)

// Runs the CLI, returns its exit status (-1 if it did not exit normally).
static int cli(const std::string& args) {
    const int rc = std::system((q(g_cli) + " " + args + " >/dev/null 2>&1").c_str());
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : -1;
}

static std::string p(const std::string& name) { return g_dir + "/" + name; }

static std::string slurp(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

static cv::Mat makeImage(int w, int h) {
    cv::Mat m(h, w, CV_8UC3);
    cv::RNG rng(321);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 3; ++c)
                m.at<cv::Vec3b>(y, x)[c] = cv::saturate_cast<uint8_t>(
                    128 + 70 * std::sin(x / 37.0 + c) * std::cos(y / 53.0 - c) + rng.gaussian(6.0));
    return m;
}

static stego::AudioData makeAudio() {
    stego::AudioData a;
    a.channels = 2;
    a.sampleRate = 44100;
    a.sfFormat = SF_FORMAT_WAV | SF_FORMAT_PCM_16;
    std::mt19937 rng(5);
    a.samples.resize(44100 * 3 * 2);
    for (size_t i = 0; i < a.samples.size() / 2; ++i)
        for (int c = 0; c < 2; ++c)
            a.samples[i * 2 + static_cast<size_t>(c)] = static_cast<int16_t>(
                8000 * std::sin(2 * M_PI * 440 * static_cast<double>(i) / 44100.0 * (c + 1)) +
                static_cast<int>(rng() % 200) - 100);
    return a;
}

int main(int argc, char** argv) {
    CHECK(argc == 2);
    g_cli = argv[1];
    g_dir = (fs::temp_directory_path() / ("stego_cli_e2e_" + std::to_string(getpid()))).string();
    fs::create_directories(g_dir);

    // inputs
    CHECK(cv::imwrite(p("cover.png"), makeImage(1024, 768)));
    CHECK(cv::imwrite(p("tiny.png"), makeImage(64, 64)));
    stego::writeAudio(p("cover.wav"), makeAudio());
    {
        std::mt19937 rng(77);
        std::ofstream f(p("msg.bin"), std::ios::binary);
        for (int i = 0; i < 300; ++i) f.put(static_cast<char>(rng()));
    }
    const std::string msg = slurp(p("msg.bin"));
    CHECK(msg.size() == 300);

    // keys
    CHECK(cli("keygen --out " + q(p("k"))) == 0);
    CHECK(fs::exists(p("k.pub")) && fs::exists(p("k.key")));
    CHECK(cli("keygen --out " + q(p("k"))) == 5);            // refuses to overwrite
    CHECK(cli("keygen --out " + q(p("k")) + " --force") == 0);
    CHECK(cli("keygen --out " + q(p("other"))) == 0);

    struct Case { const char* algo; const char* cover; const char* out; const char* extra; };
    const Case cases[] = {
        {"adaptive-lsb", "cover.png", "s_lsb.png", ""},
        {"adaptive-lsb", "cover.png", "s_lsb2.png", "--bits 2 --threshold 24"},
        {"block-dct", "cover.png", "s_dct.png", ""},
        {"audio-lsb", "cover.wav", "s_audio.wav", ""},
    };
    for (const auto& c : cases) {
        // extra options (except --strength) are part of the profile and are repeated for extract
        const std::string common = std::string("--algo ") + c.algo + " " + c.extra;
        CHECK(cli("capacity " + common + " --in " + q(p(c.cover))) == 0);
        CHECK(cli("embed " + common + " --in " + q(p(c.cover)) + " --out " + q(p(c.out)) + " --pub " +
                  q(p("k.pub")) + " --file " + q(p("msg.bin"))) == 0);
        CHECK(cli("extract " + common + " --in " + q(p(c.out)) + " --key " + q(p("k.key")) + " --out " +
                  q(p("got.bin"))) == 0);
        CHECK(slurp(p("got.bin")) == msg);
        std::printf("%s %s: roundtrip OK\n", c.algo, c.extra);

        // wrong key -> exit 3, whichever exception the algorithm raised internally
        CHECK(cli("extract " + common + " --in " + q(p(c.out)) + " --key " + q(p("other.key"))) == 3);
        // a file with no message -> exit 3
        CHECK(cli("extract " + common + " --in " + q(p(c.cover)) + " --key " + q(p("k.key"))) == 3);
    }

    // --msg path and stdout extraction
    CHECK(cli("embed --algo adaptive-lsb --in " + q(p("cover.png")) + " --out " + q(p("s_txt.png")) + " --pub " +
              q(p("k.pub")) + " --msg 'hello, world'") == 0);
    CHECK(std::system((q(g_cli) + " extract --algo adaptive-lsb --in " + q(p("s_txt.png")) + " --key " +
                       q(p("k.key")) + " 2>/dev/null > " + q(p("stdout.txt"))).c_str()) == 0);
    CHECK(slurp(p("stdout.txt")) == "hello, world");

    // analyze + visualisations
    CHECK(cli("analyze --cover " + q(p("cover.png")) + " --stego " + q(p("s_lsb.png")) + " --lsb-out " +
              q(p("lsb.png")) + " --diff-out " + q(p("diff.png"))) == 0);
    CHECK(fs::exists(p("lsb.png")) && fs::exists(p("diff.png")));

    // failure paths and exit codes
    CHECK(cli("embed --algo block-dct --in " + q(p("tiny.png")) + " --out " + q(p("x.png")) + " --pub " +
              q(p("k.pub")) + " --file " + q(p("msg.bin"))) == 4);                      // does not fit
    CHECK(cli("embed --algo adaptive-lsb --in " + q(p("cover.png")) + " --out " + q(p("x.jpg")) + " --pub " +
              q(p("k.pub")) + " --msg hi") == 2);                                       // lossy output refused
    CHECK(cli("embed --algo adaptive-lsb --in " + q(p("cover.png")) + " --out " + q(p("x.png")) + " --pub " +
              q(p("k.pub"))) == 2);                                                     // no message given
    CHECK(cli("embed --algo adaptive-lsb --pairs 2 --in " + q(p("cover.png")) + " --out " + q(p("x.png")) +
              " --pub " + q(p("k.pub")) + " --msg hi") == 2);                           // option of another algo
    CHECK(cli("extract --algo block-dct --strength 12 --in x --key y") == 2);           // encoder-only option
    CHECK(cli("extract --algo adaptive-lsb --in " + q(p("missing.png")) + " --key " + q(p("k.key"))) == 5);
    CHECK(cli("nonsense") == 2);
    CHECK(cli("help") == 0);

    fs::remove_all(g_dir);
    std::puts("cli_e2e_test: OK");
    return 0;
}
#endif
