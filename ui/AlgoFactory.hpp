#pragma once
#include "AlgoSettings.hpp"
#include "stego/IStegoAlgorithm.hpp"

#include <memory>

namespace app {

// Builds the algorithm for validated settings. Throws std::invalid_argument on bad parameters.
std::shared_ptr<const stego::IStegoAlgorithm> makeAlgorithm(const AlgoSettings& s);

}  // namespace app
