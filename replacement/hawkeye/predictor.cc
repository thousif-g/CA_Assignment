#include "predictor.h"

#include <stdexcept>

HawkeyePredictor::HawkeyePredictor(std::size_t num_entries,
                                   int counter_bits)
    : num_entries_(num_entries),
      counter_bits_(counter_bits),
      max_counter_((1 << counter_bits) - 1),
      counters_(num_entries, 0)
{
    if (num_entries == 0) {
        throw std::invalid_argument(
            "HawkeyePredictor requires at least one entry");
    }

    if (counter_bits <= 0 || counter_bits >= 31) {
        throw std::invalid_argument(
            "Invalid predictor counter width");
    }
}

std::size_t HawkeyePredictor::index(uint64_t pc) const
{
    /*
     * Deterministic PC hash.
     *
     * The assignment specifies a hashed PC and a 13-bit
     * index for the 8K-entry predictor.
     */
    uint64_t x = pc;

    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;

    return static_cast<std::size_t>(
        x % num_entries_);
}

void HawkeyePredictor::train(uint64_t pc, bool opt_hit)
{
    const std::size_t idx = index(pc);

    if (opt_hit) {
        if (counters_[idx] < max_counter_) {
            ++counters_[idx];
        }
    } else {
        if (counters_[idx] > 0) {
            --counters_[idx];
        }
    }
}

bool HawkeyePredictor::predict(uint64_t pc) const
{
    const std::size_t idx = index(pc);

    /*
     * The high-order bit determines classification.
     *
     * For the required 3-bit counter:
     *   0..3 -> cache-averse
     *   4..7 -> cache-friendly
     */
    const int high_bit =
        1 << (counter_bits_ - 1);

    return counters_[idx] >= high_bit;
}

int HawkeyePredictor::get_counter(uint64_t pc) const
{
    return counters_[index(pc)];
}