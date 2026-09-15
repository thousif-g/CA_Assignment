#ifndef HAWKEYE_PREDICTOR_H
#define HAWKEYE_PREDICTOR_H

#include <cstddef>
#include <cstdint>
#include <vector>

class HawkeyePredictor {
public:
    // num_entries: number of entries in the PC-indexed table
    // counter_bits: width of each saturating counter
    HawkeyePredictor(std::size_t num_entries = 8192,
                     int counter_bits = 3);

    // Train using the OPTgen result.
    // opt_hit=true  -> move toward cache-friendly.
    // opt_hit=false -> move toward cache-averse.
    void train(uint64_t pc, bool opt_hit);

    // Returns true for cache-friendly and false for cache-averse.
    bool predict(uint64_t pc) const;

    // Returns the raw counter value.
    int get_counter(uint64_t pc) const;

private:
    std::size_t index(uint64_t pc) const;

    std::size_t num_entries_;
    int counter_bits_;
    int max_counter_;
    std::vector<int> counters_;
};

#endif