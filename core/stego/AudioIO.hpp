#pragma once
#include "stego/IStegoAlgorithm.hpp"

#include <string>

namespace stego {

// Lossless containers only (WAV, FLAC, AIFF): a lossy codec would destroy the payload.
// Samples are read as 16-bit PCM (libsndfile converts other bit depths, which loses the
// extra precision), and written back as 16-bit PCM in the same container type.
// Throws std::runtime_error / std::invalid_argument with a readable message.
AudioData readAudio(const std::string& path);
void writeAudio(const std::string& path, const AudioData& audio);

}  // namespace stego
