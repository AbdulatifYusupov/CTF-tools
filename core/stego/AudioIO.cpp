#include "stego/AudioIO.hpp"

#include <sndfile.h>

#include <stdexcept>

namespace stego {
namespace {

struct SfHandle {
    SNDFILE* f = nullptr;
    ~SfHandle() { if (f) sf_close(f); }
};

bool isLosslessContainer(int format) {
    const int major = format & SF_FORMAT_TYPEMASK;
    return major == SF_FORMAT_WAV || major == SF_FORMAT_FLAC || major == SF_FORMAT_AIFF;
}

}  // namespace

AudioData readAudio(const std::string& path) {
    SF_INFO info{};
    SfHandle h{sf_open(path.c_str(), SFM_READ, &info)};
    if (!h.f) throw std::runtime_error(std::string("cannot open audio: ") + sf_strerror(nullptr));
    if (!isLosslessContainer(info.format))
        throw std::invalid_argument("only lossless WAV / FLAC / AIFF are supported");
    if (info.channels < 1 || info.frames <= 0) throw std::runtime_error("empty audio file");

    AudioData a;
    a.sampleRate = info.samplerate;
    a.channels   = info.channels;
    a.sfFormat   = info.format;
    a.samples.resize(static_cast<size_t>(info.frames) * static_cast<size_t>(info.channels));
    if (sf_readf_short(h.f, a.samples.data(), info.frames) != info.frames)
        throw std::runtime_error("short read from audio file");
    return a;
}

void writeAudio(const std::string& path, const AudioData& a) {
    if (a.samples.empty() || a.channels < 1 || a.sampleRate < 1)
        throw std::invalid_argument("invalid audio data");
    if (!isLosslessContainer(a.sfFormat))
        throw std::invalid_argument("only lossless WAV / FLAC / AIFF are supported");

    SF_INFO info{};
    info.samplerate = a.sampleRate;
    info.channels   = a.channels;
    info.format     = (a.sfFormat & SF_FORMAT_TYPEMASK) | SF_FORMAT_PCM_16;
    if (!sf_format_check(&info)) throw std::runtime_error("unsupported audio format combination");

    SfHandle h{sf_open(path.c_str(), SFM_WRITE, &info)};
    if (!h.f) throw std::runtime_error(std::string("cannot write audio: ") + sf_strerror(nullptr));
    const sf_count_t frames = static_cast<sf_count_t>(a.samples.size() / static_cast<size_t>(a.channels));
    if (sf_writef_short(h.f, a.samples.data(), frames) != frames)
        throw std::runtime_error("short write to audio file");
}

}  // namespace stego
