#ifndef HAWKEYE_OPTGEN_H
#define HAWKEYE_OPTGEN_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

class OPTgen
{
public:
    struct AccessResult
    {
        bool opt_hit;
        bool has_previous;
        uint64_t previous_pc;
    };

    // num_sets: number of cache sets tracked independently
    // associativity: cache associativity W
    // history_multiplier: history length in units of cache capacity
    // The assignment/paper uses 8x by default.
    OPTgen(std::size_t num_sets,
           std::size_t associativity,
           std::size_t history_multiplier = 8);
    bool access(std::size_t set_idx, uint64_t address);

    // Process one access.
    //
    // opt_hit:
    //   true  -> OPTgen predicts that the previous occurrence
    //            of this address would still be resident.
    //   false -> OPTgen predicts that it would have been evicted.
    //
    // has_previous:
    //   true if this address has appeared previously in the
    //   tracked history.
    //
    // previous_pc:
    //   PC that generated the previous access to this address.
    AccessResult access(std::size_t set_idx,
                        uint64_t address,
                        uint64_t current_pc);

private:
    struct SetState
    {
        // Occupancy values for the recent history window.
        // The oldest entry is at the front.
        std::deque<int> occupancy;

        // Sequence number of the most recent access to each address.
        std::unordered_map<uint64_t, std::size_t> last_access;

        // PC responsible for the most recent access to each address.
        std::unordered_map<uint64_t, uint64_t> last_pc;

        // Number of accesses processed for this set.
        std::size_t timestamp = 0;
    };

    std::size_t associativity_;
    std::size_t history_length_;
    std::vector<SetState> sets_;
};

#endif