#include "AlgoFactory.hpp"

#include "stego/AdaptiveLsb.hpp"
#include "stego/BlockDct.hpp"

#include <stdexcept>

namespace app {

std::shared_ptr<const stego::IStegoAlgorithm> makeAlgorithm(const AlgoSettings& s) {
    if (const std::string err = validate(s); !err.empty()) throw std::invalid_argument(err);
    if (s.algo == "adaptive-lsb") {
        stego::AdaptiveLsb::Options o;
        o.bitsPerChannel = s.bits;
        o.threshold      = s.threshold;
        return std::make_shared<const stego::AdaptiveLsb>(o);
    }
    stego::BlockDct::Options o;
    o.pairsPerBlock = s.pairs;
    o.paritySymbols = s.parity;
    o.strength      = s.strength;
    return std::make_shared<const stego::BlockDct>(o);
}

}  // namespace app
