// stego_cli: command line front end for the stego core.
//
// Exit codes:  0 ok | 1 other error | 2 bad arguments | 3 nothing found / wrong key / damaged file
//              4 message does not fit | 5 file I/O error | 130 cancelled
#include "Args.hpp"
#include "Profile.hpp"

#include "analysis/LsbDetection.hpp"
#include "analysis/Metrics.hpp"
#include "crypto/Crypto.hpp"
#include "stego/AdaptiveLsb.hpp"
#include "stego/AudioIO.hpp"
#include "stego/AudioLsb.hpp"
#include "stego/BlockDct.hpp"
#include "stego/Pipeline.hpp"

#include <opencv2/imgcodecs.hpp>

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace {

enum Exit { kOk = 0, kFail = 1, kUsage = 2, kExtract = 3, kCapacity = 4, kIo = 5, kCancelled = 130 };

class IoError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// The single error for "could not get a message out", whatever the algorithm threw internally
// (crypto::Error from the length/tag check, std::runtime_error from Reed-Solomon, ...).
class ExtractFailure : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

using Bytes = std::vector<uint8_t>;

const char* kUsageText = R"(stego_cli - hybrid post-quantum steganography (ML-KEM + ChaCha20-Poly1305)

Commands
  keygen   --out PREFIX [--force]
           Writes PREFIX.pub and PREFIX.key (DER). Keep the .key file secret.
  capacity --algo A --in COVER [--mode kem|password] [algorithm options | --profile FILE.json]
  embed    --algo A --in COVER --out STEGO  SECRET  (--msg TEXT | --file PATH|-)
           [algorithm options] [--profile-out FILE.json] [--progress]
  extract  --in STEGO  SECRET  [--out PATH] (--algo A [algorithm options] | --profile FILE.json) [--progress]
           Without --out the message goes to stdout.

  SECRET (exactly one) selects the envelope mode; the file does not say which one was used:
    KEM mode       embed: --pub PREFIX.pub        extract: --key PREFIX.key
                   Recipient key pair, 1088-byte header, no offline guessing attack.
    password mode  --password-file PATH|-   (recommended; one trailing newline is ignored)
                   --ask-password           (prompts without echo; asked twice when embedding)
                   --password TEXT          (visible in the process list, avoid)
                   Argon2id (64 MiB, 3 passes), 16-byte header: far more room on small images.
                   Security = password strength; an attacker holding the file can guess offline.
  analyze  --cover ORIGINAL --stego STEGO [--lsb-out FILE.png] [--diff-out FILE.png]
           PSNR, SSIM, chi-square, and blind LSB-replacement estimates (RS, sample pairs) per channel.

Algorithms (--algo)
  adaptive-lsb   images       --bits 1|2 (default 1)  --threshold N (default 32)
  audio-lsb      WAV/FLAC     --bits 1|2 (default 1)  --threshold N (default 64)
  block-dct      images       --pairs 1..3 (default 2)  --parity N (default 32)
                              --strength 8..100 (embed only, default 12)

Rules
  * Algorithm options (except --strength) must be IDENTICAL at embed and extract.
    Instead of repeating them, save a profile with  embed --profile-out p.json  and pass
    --profile p.json to extract/capacity. It holds the algorithm, its parameters and the mode,
    but no secrets, so it can travel next to the file. Explicit options that contradict it are errors.
  * Images are written losslessly (.png, .bmp, .tif); audio as .wav, .flac or .aiff.
    Any lossy re-encode (JPEG, MP3, messenger compression) destroys the message.

Exit codes
  0 ok | 2 bad arguments | 3 no message / wrong key / damaged | 4 does not fit | 5 I/O error
)";

// ---- files -----------------------------------------------------------------------------------------

Bytes readAll(const std::string& path) {
    if (path == "-") {
        return Bytes(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) throw IoError("cannot read file: " + path);
    return Bytes(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

void writeAll(const std::string& path, const Bytes& data) {
    if (path.empty() || path == "-") {
        std::cout.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        std::cout.flush();
        return;
    }
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw IoError("cannot write file: " + path);
    f.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!f) throw IoError("write failed: " + path);
}

std::string extensionOf(const std::string& path) {
    std::string e = fs::path(path).extension().string();
    std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return e;
}

// ---- algorithm selection ---------------------------------------------------------------------------

struct AlgoChoice {
    std::shared_ptr<const stego::IStegoAlgorithm> algo;
    bool audio = false;
    std::string name;
    std::string profile;  // what must be identical at extraction
    cli::ProfileMap params;  // the same, machine readable (for --profile-out)
};

void rejectUnused(const cli::Args& a, const std::string& algo, std::initializer_list<const char*> names) {
    for (const char* n : names)
        if (a.has(n)) throw cli::UsageError(std::string("option --") + n + " is not used by " + algo);
}

AlgoChoice makeAlgo(const cli::Args& a) {
    AlgoChoice c;
    c.name = a.require("algo");
    if (c.name == "adaptive-lsb") {
        rejectUnused(a, c.name, {"pairs", "parity", "strength"});
        stego::AdaptiveLsb::Options o;
        o.bitsPerChannel = a.getInt("bits", o.bitsPerChannel);
        o.threshold      = a.getInt("threshold", o.threshold);
        c.algo           = std::make_shared<const stego::AdaptiveLsb>(o);
        c.profile        = "bits=" + std::to_string(o.bitsPerChannel) + " threshold=" + std::to_string(o.threshold);
        c.params         = {{"bits", std::to_string(o.bitsPerChannel)}, {"threshold", std::to_string(o.threshold)}};
    } else if (c.name == "audio-lsb") {
        rejectUnused(a, c.name, {"pairs", "parity", "strength"});
        stego::AudioLsb::Options o;
        o.bitsPerSample = a.getInt("bits", o.bitsPerSample);
        o.threshold     = a.getInt("threshold", o.threshold);
        c.algo          = std::make_shared<const stego::AudioLsb>(o);
        c.audio         = true;
        c.profile       = "bits=" + std::to_string(o.bitsPerSample) + " threshold=" + std::to_string(o.threshold);
        c.params        = {{"bits", std::to_string(o.bitsPerSample)}, {"threshold", std::to_string(o.threshold)}};
    } else if (c.name == "block-dct") {
        rejectUnused(a, c.name, {"bits", "threshold"});
        stego::BlockDct::Options o;
        o.pairsPerBlock = a.getInt("pairs", o.pairsPerBlock);
        o.paritySymbols = a.getInt("parity", o.paritySymbols);
        o.strength      = a.getInt("strength", o.strength);
        c.algo          = std::make_shared<const stego::BlockDct>(o);
        c.profile       = "pairs=" + std::to_string(o.pairsPerBlock) + " parity=" + std::to_string(o.paritySymbols);
        c.params        = {{"pairs", std::to_string(o.pairsPerBlock)}, {"parity", std::to_string(o.paritySymbols)}};
    } else {
        throw cli::UsageError("unknown algorithm '" + c.name + "' (adaptive-lsb | audio-lsb | block-dct)");
    }
    return c;
}

// ---- carriers --------------------------------------------------------------------------------------

cv::Mat loadImage(const std::string& path) {
    const cv::Mat m = cv::imread(path, cv::IMREAD_UNCHANGED);
    if (m.empty()) throw IoError("cannot read image: " + path);
    if (m.depth() != CV_8U || (m.channels() != 1 && m.channels() != 3 && m.channels() != 4))
        throw std::invalid_argument("only 8-bit gray/BGR/BGRA images are supported: " + path);
    return m;
}

stego::Carrier loadCarrier(const std::string& path, bool audio) {
    if (!audio) return loadImage(path);
    try {
        return stego::readAudio(path);
    } catch (const std::invalid_argument&) {
        throw;  // lossy / unsupported container: a usage problem
    } catch (const std::runtime_error& e) {
        throw IoError(e.what());
    }
}

// Returns the libsndfile container for an audio output path (0 for images). Throws UsageError
// for lossy/unknown extensions, so callers can fail BEFORE doing the expensive work.
int checkOutputPath(const std::string& path, bool audio) {
    const std::string ext = extensionOf(path);
    if (!audio) {
        if (ext != ".png" && ext != ".bmp" && ext != ".tif" && ext != ".tiff")
            throw cli::UsageError("output image must be lossless (.png, .bmp, .tif), got '" + ext + "'");
        return 0;
    }
    if (ext == ".wav") return SF_FORMAT_WAV;
    if (ext == ".flac") return SF_FORMAT_FLAC;
    if (ext == ".aif" || ext == ".aiff") return SF_FORMAT_AIFF;
    throw cli::UsageError("output audio must be .wav, .flac or .aiff, got '" + ext + "'");
}

void saveCarrier(const std::string& path, const stego::Carrier& carrier, bool audio) {
    const int major = checkOutputPath(path, audio);
    if (!audio) {
        if (!cv::imwrite(path, std::get<cv::Mat>(carrier))) throw IoError("cannot write image: " + path);
        return;
    }
    stego::AudioData a = std::get<stego::AudioData>(carrier);
    a.sfFormat         = major | SF_FORMAT_PCM_16;
    try {
        stego::writeAudio(path, a);
    } catch (const std::runtime_error& e) {
        throw IoError(e.what());
    }
}

stego::Progress makeProgress(bool on, const char* label) {
    if (!on) return {};
    return [label](float f) {
        std::fprintf(stderr, "\r%s %3d%%", label, static_cast<int>(f * 100.f));
        return true;
    };
}

// ---- secrets: KEM key file or password ------------------------------------------------------------

struct Secret {
    bool password = false;
    std::string text;  // password mode
    Bytes key;         // KEM mode: DER public key (embed) or private key (extract)
    size_t headerBytes() const { return password ? stego::kPasswordHeaderBytes : stego::kKemHeaderBytes; }
};

std::string stripLineEnd(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
    return s;
}

#ifndef _WIN32
std::string promptHidden(const char* prompt) {
    FILE* tty = std::fopen("/dev/tty", "r+");
    if (!tty) throw IoError("no terminal available for --ask-password (use --password-file)");
    const int fd = fileno(tty);
    termios saved{}, quiet{};
    tcgetattr(fd, &saved);
    quiet = saved;
    quiet.c_lflag &= static_cast<tcflag_t>(~ECHO);
    tcsetattr(fd, TCSAFLUSH, &quiet);
    std::fputs(prompt, tty);
    std::fflush(tty);
    char buf[1024];
    std::string line;
    if (std::fgets(buf, sizeof buf, tty)) line = buf;
    tcsetattr(fd, TCSAFLUSH, &saved);
    std::fputs("\n", tty);
    std::fclose(tty);
    return stripLineEnd(line);
}
#else
std::string promptHidden(const char*) { throw IoError("--ask-password is not supported on Windows; use --password-file"); }
#endif

// Picks exactly one secret source. `keyOption` is "pub" (embed) or "key" (extract).
Secret readSecret(const cli::Args& a, const char* keyOption, bool confirmPrompt) {
    const int sources = int(a.has(keyOption)) + int(a.has("password")) + int(a.has("password-file")) +
                        int(a.flag("ask-password"));
    if (sources != 1)
        throw cli::UsageError(std::string("give exactly one of --") + keyOption +
                              ", --password-file, --ask-password, --password");
    Secret s;
    if (a.has(keyOption)) {
        s.key = readAll(a.get(keyOption));
        if (s.key.empty()) throw IoError(std::string("empty key file: ") + a.get(keyOption));
        return s;
    }
    s.password = true;
    if (a.has("password")) {
        s.text = a.get("password");
        std::fprintf(stderr, "note: --password is visible in the process list; prefer --password-file or --ask-password\n");
    } else if (a.has("password-file")) {
        const Bytes raw = readAll(a.get("password-file"));
        s.text = stripLineEnd(std::string(raw.begin(), raw.end()));
    } else {
        s.text = promptHidden("password: ");
        if (confirmPrompt && promptHidden("repeat password: ") != s.text)
            throw cli::UsageError("the passwords do not match");
    }
    if (s.text.empty()) throw cli::UsageError("the password is empty");
    return s;
}

void printChi(const char* label, const analysis::ChiSquareResult& r) {
    std::printf("%-10s chi2=%.1f dof=%d p(embedded)=%.4f\n", label, r.chi2, r.dof, r.pEmbedded);
}

// RS / sample-pairs line: mean over the valid channels, then the per-channel values (B G R).
// '*' marks an estimate that came from the double-root fallback (p close to 1).
void printLsb(const char* name, const char* label, const std::vector<analysis::LsbDetection>& d, bool rs) {
    static const char* const kNames[] = {"B", "G", "R"};
    double sum = 0;
    int n      = 0;
    std::string per;
    for (size_t i = 0; i < d.size(); ++i) {
        const analysis::LsbEstimate& e = rs ? d[i].rs : d[i].spa;
        char buf[48];
        if (e.valid) {
            std::snprintf(buf, sizeof buf, "%s%s%s%.3f%s", per.empty() ? "" : " ", d.size() == 3 ? kNames[i] : "",
                          d.size() == 3 ? " " : "", e.p, e.approx ? "*" : "");
            sum += e.p;
            ++n;
        } else {
            std::snprintf(buf, sizeof buf, "%s%s%sn/a", per.empty() ? "" : " ", d.size() == 3 ? kNames[i] : "",
                          d.size() == 3 ? " " : "");
        }
        per += buf;
    }
    if (n == 0) std::printf("%-3s %-6s: n/a  [%s]\n", name, label, per.c_str());
    else std::printf("%-3s %-6s: p=%.3f  [%s]\n", name, label, sum / n, per.c_str());
}

// ---- commands --------------------------------------------------------------------------------------

int cmdKeygen(const cli::Args& a) {
    const std::string prefix = a.require("out");
    const std::string pubPath = prefix + ".pub", keyPath = prefix + ".key";
    if (!a.flag("force") && (fs::exists(pubPath) || fs::exists(keyPath)))
        throw IoError("key files already exist (use --force to overwrite): " + prefix + ".{pub,key}");

    const auto kp = crypto::generateKeyPair();
    writeAll(pubPath, kp.publicKey);
    writeAll(keyPath, kp.privateKey);
    std::error_code ec;  // best effort: owner-only access to the private key
    fs::permissions(keyPath, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);

    std::printf("public key : %s (%zu bytes)\nprivate key: %s (%zu bytes, keep it secret)\n", pubPath.c_str(),
                kp.publicKey.size(), keyPath.c_str(), kp.privateKey.size());
    return kOk;
}

int cmdCapacity(const cli::Args& a) {
    const auto algo      = makeAlgo(a);
    const auto carrier   = loadCarrier(a.require("in"), algo.audio);
    const std::string mode = a.get("mode", "kem");
    if (mode != "kem" && mode != "password") throw cli::UsageError("--mode must be 'kem' or 'password'");
    const size_t header = mode == "kem" ? stego::kKemHeaderBytes : stego::kPasswordHeaderBytes;

    std::printf("algorithm  : %s (%s)\nmode       : %s\n", algo.name.c_str(), algo.profile.c_str(), mode.c_str());
    std::printf("capacity   : %zu bits\nmax message: %zu bytes\n", algo.algo->capacityBits(carrier, header),
                stego::maxPayloadBytes(*algo.algo, carrier, header));
    if (mode == "kem") {
        std::printf("password   : %zu bytes max with --mode password (smaller header)\n",
                    stego::maxPayloadBytes(*algo.algo, carrier, stego::kPasswordHeaderBytes));
    }
    return kOk;
}

int cmdEmbed(const cli::Args& a) {
    const auto algo = makeAlgo(a);
    if (a.has("msg") == a.has("file")) throw cli::UsageError("give exactly one of --msg or --file");
    const std::string out = a.require("out");
    checkOutputPath(out, algo.audio);  // fail fast on a lossy extension, before any heavy work
    const Secret secret = readSecret(a, "pub", /*confirmPrompt=*/true);  // and on bad secret options
    if (secret.password && secret.text.size() < 12)
        std::fprintf(stderr, "warning: short password (%zu characters). Anyone holding the file can try guesses offline.\n",
                     secret.text.size());

    Bytes payload;
    if (a.has("msg")) {
        const std::string m = a.get("msg");
        payload.assign(m.begin(), m.end());
    } else {
        payload = readAll(a.get("file"));
    }
    if (payload.empty()) throw cli::UsageError("the message is empty");
    if (payload.size() > crypto::kMaxPayload) throw std::length_error("message too large");

    stego::Carrier cover   = loadCarrier(a.require("in"), algo.audio);
    stego::Carrier carrier = cover;
    if (!algo.audio) carrier = std::get<cv::Mat>(cover).clone();  // embed works in place

    const size_t cap  = algo.algo->capacityBits(cover, secret.headerBytes());
    const size_t used = (secret.headerBytes() + crypto::kLengthFieldSize + payload.size() + crypto::kTagSize) * 8;

    const auto progress = makeProgress(a.flag("progress"), "embedding");
    if (secret.password) stego::embedMessageWithPassword(*algo.algo, carrier, payload, secret.text, progress);
    else stego::embedMessage(*algo.algo, carrier, payload, secret.key, progress);
    if (a.flag("progress")) std::fprintf(stderr, "\n");

    saveCarrier(out, carrier, algo.audio);

    std::printf("algorithm : %s\nprofile   : %s   (reuse at extraction)\n", algo.name.c_str(), algo.profile.c_str());
    std::printf("mode      : %s\n", secret.password ? "password (Argon2id 64 MiB x3), extract with the same password"
                                                    : "kem (ML-KEM-768), extract with the matching .key");
    std::printf("capacity  : %zu bits, used %zu bits (%.1f%%)\n", cap, used, 100.0 * static_cast<double>(used) / static_cast<double>(cap));

    if (!algo.audio) {
        const cv::Mat& before = std::get<cv::Mat>(cover);
        const cv::Mat& after  = std::get<cv::Mat>(carrier);
        std::printf("quality   : PSNR %.2f dB", analysis::psnr(before, after));
        if (before.rows >= 11 && before.cols >= 11) std::printf(", SSIM %.4f", analysis::ssim(before, after));
        std::printf("\n");
        printChi("chi before", analysis::chiSquare(before));
        printChi("chi after ", analysis::chiSquare(after));
    } else {
        const auto& x = std::get<stego::AudioData>(cover).samples;
        const auto& y = std::get<stego::AudioData>(carrier).samples;
        size_t changed = 0;
        int maxDelta   = 0;
        for (size_t i = 0; i < x.size(); ++i)
            if (x[i] != y[i]) {
                ++changed;
                maxDelta = std::max(maxDelta, std::abs(x[i] - y[i]));
            }
        std::printf("changed   : %zu of %zu samples (%.2f%%), max delta %d\n", changed, x.size(),
                    100.0 * static_cast<double>(changed) / static_cast<double>(x.size()), maxDelta);
    }
    std::printf("written   : %s\n", out.c_str());

    if (a.has("profile-out")) {
        cli::ProfileMap pm = algo.params;
        pm["algo"]         = algo.name;
        pm["mode"]         = secret.password ? "password" : "kem";
        const std::string json = cli::profileToJson(pm);
        writeAll(a.get("profile-out"), Bytes(json.begin(), json.end()));
        std::printf("profile   : %s (no secrets inside)\n", a.get("profile-out").c_str());
    }
    return kOk;
}

int cmdExtract(const cli::Args& a) {
    const auto algo    = makeAlgo(a);
    const Secret secret = readSecret(a, "key", /*confirmPrompt=*/false);
    if (const std::string pm = a.get("profile-mode"); !pm.empty() && (pm == "password") != secret.password)
        throw cli::UsageError("the profile says mode '" + pm + "', but the secret options select the other mode");
    const auto carrier  = loadCarrier(a.require("in"), algo.audio);

    const auto progress = makeProgress(a.flag("progress"), "extracting");
    Bytes message;
    try {
        message = secret.password ? stego::extractMessageWithPassword(*algo.algo, carrier, secret.text, progress)
                                  : stego::extractMessage(*algo.algo, carrier, secret.key, progress);
    } catch (const stego::OperationCancelled&) {
        throw;
    } catch (const std::runtime_error& e) {
        throw ExtractFailure(std::string("no message found: wrong key/password or parameters, wrong mode, no hidden data, "
                                         "or the file was modified (") + e.what() + ")");
    } catch (const std::length_error&) {
        throw ExtractFailure("the file is too small to contain a message");
    }
    if (a.flag("progress")) std::fprintf(stderr, "\n");

    writeAll(a.get("out"), message);
    std::fprintf(stderr, "extracted %zu bytes\n", message.size());
    return kOk;
}

int cmdAnalyze(const cli::Args& a) {
    const cv::Mat cover = loadImage(a.require("cover"));
    const cv::Mat stg   = loadImage(a.require("stego"));
    if (cover.rows != stg.rows || cover.cols != stg.cols || cover.channels() != stg.channels())
        throw cli::UsageError("the two images differ in size or channel count");

    const double p = analysis::psnr(cover, stg);
    if (std::isinf(p)) std::printf("PSNR      : inf (identical)\n");
    else std::printf("PSNR      : %.2f dB\n", p);
    if (cover.rows >= 11 && cover.cols >= 11) std::printf("SSIM      : %.4f\n", analysis::ssim(cover, stg));
    printChi("chi cover ", analysis::chiSquare(cover));
    printChi("chi stego ", analysis::chiSquare(stg));
    std::printf("(chi-square is a global test: adaptive/shuffled embedding at low fill will not trigger it)\n");
    const auto detCover = analysis::lsbDetection(cover);
    const auto detStego = analysis::lsbDetection(stg);
    printLsb("RS", "cover", detCover, true);
    printLsb("RS", "stego", detStego, true);
    printLsb("SPA", "cover", detCover, false);
    printLsb("SPA", "stego", detStego, false);
    std::printf("(RS/SPA: estimated fraction of ALL samples carrying a bit, 0 = clean, noise roughly +-0.03;\n"
                " only LSB-1 replacement is modelled; '*' = no real root, double-root fallback near p=1)\n");

    auto save = [](const std::string& path, const cv::Mat& m) {
        if (extensionOf(path) != ".png") throw cli::UsageError("visualisation output must be .png: " + path);
        if (!cv::imwrite(path, m)) throw IoError("cannot write image: " + path);
        std::printf("written   : %s\n", path.c_str());
    };
    if (a.has("lsb-out")) save(a.get("lsb-out"), analysis::lsbPlane(stg, 0));
    if (a.has("diff-out")) save(a.get("diff-out"), analysis::amplifiedDiff(cover, stg, 64.0));
    return kOk;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        cli::Args args = cli::parseArgs(std::vector<std::string>(argv + 1, argv + argc));
        if (args.has("profile")) {  // capacity / extract only (see the option spec)
            const Bytes raw = readAll(args.get("profile"));
            cli::applyProfile(args, cli::parseProfileJson(std::string(raw.begin(), raw.end())));
        }
        if (args.command == "help") {
            std::fputs(kUsageText, stdout);
            return kOk;
        }
        if (args.command == "keygen") return cmdKeygen(args);
        if (args.command == "capacity") return cmdCapacity(args);
        if (args.command == "embed") return cmdEmbed(args);
        if (args.command == "extract") return cmdExtract(args);
        if (args.command == "analyze") return cmdAnalyze(args);
        return kUsage;  // unreachable: parseArgs only accepts known commands
    } catch (const cli::UsageError& e) {
        std::fprintf(stderr, "error: %s\nrun 'stego_cli help' for usage\n", e.what());
        return kUsage;
    } catch (const ExtractFailure& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return kExtract;
    } catch (const IoError& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return kIo;
    } catch (const stego::OperationCancelled&) {
        std::fprintf(stderr, "cancelled\n");
        return kCancelled;
    } catch (const std::length_error& e) {
        std::fprintf(stderr, "error: the message does not fit into this file (%s)\n", e.what());
        return kCapacity;
    } catch (const std::invalid_argument& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return kUsage;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return kFail;
    }
}
