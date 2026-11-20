// stego_cli: command line front end for the stego core.
//
// Exit codes:  0 ok | 1 other error | 2 bad arguments | 3 nothing found / wrong key / damaged file
//              4 message does not fit | 5 file I/O error | 130 cancelled
#include "Args.hpp"

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
  capacity --algo A --in COVER [algorithm options]
  embed    --algo A --in COVER --out STEGO --pub PREFIX.pub (--msg TEXT | --file PATH|-)
           [algorithm options] [--progress]
  extract  --algo A --in STEGO --key PREFIX.key [--out PATH] [algorithm options] [--progress]
           Without --out the message goes to stdout.
  analyze  --cover ORIGINAL --stego STEGO [--lsb-out FILE.png] [--diff-out FILE.png]

Algorithms (--algo)
  adaptive-lsb   images       --bits 1|2 (default 1)  --threshold N (default 32)
  audio-lsb      WAV/FLAC     --bits 1|2 (default 1)  --threshold N (default 64)
  block-dct      images       --pairs 1..3 (default 2)  --parity N (default 32)
                              --strength 8..100 (embed only, default 12)

Rules
  * Algorithm options (except --strength) must be IDENTICAL at embed and extract.
    embed prints the profile line you need to reuse.
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
    } else if (c.name == "audio-lsb") {
        rejectUnused(a, c.name, {"pairs", "parity", "strength"});
        stego::AudioLsb::Options o;
        o.bitsPerSample = a.getInt("bits", o.bitsPerSample);
        o.threshold     = a.getInt("threshold", o.threshold);
        c.algo          = std::make_shared<const stego::AudioLsb>(o);
        c.audio         = true;
        c.profile       = "bits=" + std::to_string(o.bitsPerSample) + " threshold=" + std::to_string(o.threshold);
    } else if (c.name == "block-dct") {
        rejectUnused(a, c.name, {"bits", "threshold"});
        stego::BlockDct::Options o;
        o.pairsPerBlock = a.getInt("pairs", o.pairsPerBlock);
        o.paritySymbols = a.getInt("parity", o.paritySymbols);
        o.strength      = a.getInt("strength", o.strength);
        c.algo          = std::make_shared<const stego::BlockDct>(o);
        c.profile       = "pairs=" + std::to_string(o.pairsPerBlock) + " parity=" + std::to_string(o.paritySymbols);
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

size_t maxPayloadBytes(size_t capacityBits) {
    const size_t overhead = stego::kHeaderBytes + crypto::kLengthFieldSize + crypto::kTagSize;
    return capacityBits / 8 > overhead ? capacityBits / 8 - overhead : 0;
}

void printChi(const char* label, const analysis::ChiSquareResult& r) {
    std::printf("%-10s chi2=%.1f dof=%d p(embedded)=%.4f\n", label, r.chi2, r.dof, r.pEmbedded);
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
    const size_t cap     = algo.algo->capacityBits(carrier);
    std::printf("algorithm : %s (%s)\ncapacity  : %zu bits\nmax message: %zu bytes\n", algo.name.c_str(),
                algo.profile.c_str(), cap, maxPayloadBytes(cap));
    return kOk;
}

int cmdEmbed(const cli::Args& a) {
    const auto algo = makeAlgo(a);
    if (a.has("msg") == a.has("file")) throw cli::UsageError("give exactly one of --msg or --file");
    const std::string out = a.require("out");
    checkOutputPath(out, algo.audio);  // fail fast on a lossy extension, before any heavy work

    Bytes payload;
    if (a.has("msg")) {
        const std::string m = a.get("msg");
        payload.assign(m.begin(), m.end());
    } else {
        payload = readAll(a.get("file"));
    }
    if (payload.empty()) throw cli::UsageError("the message is empty");
    if (payload.size() > crypto::kMaxPayload) throw std::length_error("message too large");

    const Bytes pub = readAll(a.require("pub"));
    stego::Carrier cover   = loadCarrier(a.require("in"), algo.audio);
    stego::Carrier carrier = cover;
    if (!algo.audio) carrier = std::get<cv::Mat>(cover).clone();  // embed works in place

    const size_t cap  = algo.algo->capacityBits(cover);
    const size_t used = (stego::kHeaderBytes + crypto::kLengthFieldSize + payload.size() + crypto::kTagSize) * 8;

    const auto progress = makeProgress(a.flag("progress"), "embedding");
    stego::embedMessage(*algo.algo, carrier, payload, pub, progress);
    if (a.flag("progress")) std::fprintf(stderr, "\n");

    saveCarrier(out, carrier, algo.audio);

    std::printf("algorithm : %s\nprofile   : %s   (reuse at extraction)\n", algo.name.c_str(), algo.profile.c_str());
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
    return kOk;
}

int cmdExtract(const cli::Args& a) {
    const auto algo    = makeAlgo(a);
    const Bytes key    = readAll(a.require("key"));
    const auto carrier = loadCarrier(a.require("in"), algo.audio);

    const auto progress = makeProgress(a.flag("progress"), "extracting");
    Bytes message;
    try {
        message = stego::extractMessage(*algo.algo, carrier, key, progress);
    } catch (const stego::OperationCancelled&) {
        throw;
    } catch (const std::runtime_error& e) {
        throw ExtractFailure(std::string("no message found: wrong key or parameters, no hidden data, or the file was "
                                         "modified (") + e.what() + ")");
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
        const cli::Args args = cli::parseArgs(std::vector<std::string>(argv + 1, argv + argc));
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
