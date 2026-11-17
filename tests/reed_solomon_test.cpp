#include "ecc/ReedSolomon.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <numeric>
#include <random>
#include <stdexcept>

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

using Bytes = std::vector<uint8_t>;
static std::mt19937 rng(12345);

static Bytes randomBytes(size_t n) {
    Bytes b(n);
    for (auto& x : b) x = static_cast<uint8_t>(rng());
    return b;
}

// k distinct positions in [0, n)
static std::vector<int> pick(int n, int k) {
    std::vector<int> all(static_cast<size_t>(n));
    std::iota(all.begin(), all.end(), 0);
    std::shuffle(all.begin(), all.end(), rng);
    all.resize(static_cast<size_t>(k));
    return all;
}

static void corrupt(Bytes& b, int pos) {
    uint8_t d;
    do d = static_cast<uint8_t>(rng()); while (d == 0);  // guaranteed change
    b[static_cast<size_t>(pos)] ^= d;
}

int main() {
    // --- block: errors up to capacity, with and without erasures -----------------------------
    int cases = 0;
    for (int nsym : {2, 4, 8, 16, 32, 64}) {
        const ecc::ReedSolomon rs(nsym);
        for (size_t dlen : {size_t{1}, size_t{7}, size_t{50}, static_cast<size_t>(rs.maxBlockData())}) {
            for (int rep = 0; rep < 60; ++rep) {
                const Bytes data = randomBytes(dlen);
                const Bytes clean = rs.encodeBlock(data);
                const int n = static_cast<int>(clean.size());
                CHECK(std::equal(data.begin(), data.end(), clean.begin()));  // systematic

                // pure errors, 0..nsym/2
                {
                    const int e = static_cast<int>(rng() % static_cast<unsigned>(nsym / 2 + 1));
                    Bytes bad = clean;
                    for (int p : pick(n, std::min(e, n))) corrupt(bad, p);
                    const int changed = static_cast<int>(
                        std::inner_product(bad.begin(), bad.end(), clean.begin(), 0, std::plus<>(),
                                           std::not_equal_to<>()));
                    const auto r = rs.decodeBlock(bad);
                    CHECK(r.has_value() && *r == changed);
                    CHECK(bad == clean);
                    ++cases;
                }
                // mixed: f erasures + e errors with 2e + f <= nsym
                {
                    const int f = static_cast<int>(rng() % static_cast<unsigned>(nsym + 1));
                    const int e = static_cast<int>(rng() % static_cast<unsigned>((nsym - f) / 2 + 1));
                    if (f + e <= n) {
                        const auto pos = pick(n, f + e);
                        std::vector<int> erasures(pos.begin(), pos.begin() + f);
                        Bytes bad = clean;
                        for (int p : pos) corrupt(bad, p);
                        // erased symbols may also hold the right value: flagged != wrong
                        const auto r = rs.decodeBlock(bad, erasures);
                        CHECK(r.has_value());
                        CHECK(bad == clean);
                        ++cases;
                    }
                }
            }
        }
    }
    std::printf("block correction: %d random cases OK\n", cases);

    // --- beyond capacity: must fail or land on a valid codeword, never return garbage ---------
    {
        const ecc::ReedSolomon rs(16);
        int failed = 0, miscorrected = 0, total = 400;
        for (int rep = 0; rep < total; ++rep) {
            const Bytes clean = rs.encodeBlock(randomBytes(100));
            Bytes bad = clean;
            for (int p : pick(static_cast<int>(bad.size()), 9 + static_cast<int>(rng() % 20))) corrupt(bad, p);
            const Bytes before = bad;
            const auto r = rs.decodeBlock(bad);
            if (!r) {
                CHECK(bad == before);  // untouched on failure
                ++failed;
            } else {
                CHECK(rs.decodeBlock(bad) == std::optional<int>(0));  // result is a valid codeword
                if (bad != clean) ++miscorrected;
            }
        }
        std::printf("over capacity (t+1..t+20 errors): %d/%d rejected, %d miscorrected to another codeword\n",
                    failed, total, miscorrected);
        CHECK(failed > total * 9 / 10);
    }

    // --- stream: sizes, burst errors via interleaving ------------------------------------------
    for (int nsym : {16, 32}) {
        const ecc::ReedSolomon rs(nsym);
        for (size_t len : {size_t{1}, size_t{100}, size_t{223}, size_t{224}, size_t{1000}, size_t{5000}, size_t{100000}}) {
            const Bytes data = randomBytes(len);
            const Bytes enc = rs.encode(data);
            CHECK(enc.size() == rs.encodedSize(len));

            ecc::DecodeStats st;
            CHECK(rs.decode(enc, len, &st) == data && st.ok() && st.correctedSymbols == 0);

            // one contiguous burst of B*t symbols: interleaving spreads it to <= t per block
            const size_t B = st.blocks, t = static_cast<size_t>(nsym / 2);
            Bytes bad = enc;
            const size_t burst = std::min(bad.size(), B * t);
            const size_t start = rng() % (bad.size() - burst + 1);
            for (size_t i = 0; i < burst; ++i) bad[start + i] ^= static_cast<uint8_t>(1 + rng() % 255);
            const Bytes got = rs.decode(bad, len, &st);
            CHECK(got == data);
            CHECK(st.ok() && st.correctedSymbols <= burst);
        }
    }
    std::puts("stream + burst correction OK");

    // --- stream: uncorrectable damage is reported, not hidden ---------------------------------
    {
        const ecc::ReedSolomon rs(32);
        const Bytes data = randomBytes(2000);
        Bytes bad = rs.encode(data);
        for (size_t i = 0; i < bad.size(); i += 2) bad[i] ^= 0x55;  // ~50% symbols wrong
        ecc::DecodeStats st;
        const Bytes got = rs.decode(bad, data.size(), &st);
        CHECK(!st.ok() && st.failedBlocks > 0 && got.size() == data.size());
    }

    // --- argument validation ---------------------------------------------------------------------
    auto throws = [](auto&& f) { try { f(); } catch (const std::invalid_argument&) { return true; } return false; };
    CHECK(throws([] { ecc::ReedSolomon rs(0); }));
    CHECK(throws([] { ecc::ReedSolomon rs(255); }));
    CHECK(throws([] { ecc::ReedSolomon(16).encode({}); }));
    CHECK(throws([] { ecc::ReedSolomon(16).decode(Bytes(10), 100); }));
    {
        const ecc::ReedSolomon rs(8);
        Bytes b = rs.encodeBlock(randomBytes(10));
        const std::vector<int> bad{99};
        b[0] ^= 1;  // needs a non-zero syndrome, otherwise decodeBlock returns before validating
        CHECK(throws([&] { (void)rs.decodeBlock(b, bad); }));
    }

    std::puts("reed_solomon_test: OK");
    return 0;
}
