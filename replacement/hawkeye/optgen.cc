#include "optgen.h"

#include <stdexcept>

OPTgen::OPTgen(std::size_t num_sets,
               std::size_t associativity,
               std::size_t history_multiplier)
    : associativity_(associativity),
      history_length_(associativity * history_multiplier),
      sets_(num_sets)
{
    if (num_sets == 0) {
        throw std::invalid_argument(
            "OPTgen requires at least one set");
    }

    if (associativity == 0) {
        throw std::invalid_argument(
            "OPTgen requires non-zero associativity");
    }

    if (history_multiplier == 0) {
        throw std::invalid_argument(
            "OPTgen requires non-zero history multiplier");
    }

    for (auto& set : sets_) {
        set.occupancy.clear();
        set.last_access.clear();
        set.last_pc.clear();
        set.timestamp = 0;
    }
}

/*
 * Compatibility interface for the standalone OPTgen test driver.
 *
 * The standalone test does not provide a PC, so use 0 for current_pc
 * and return only the opt_hit result.
 */
bool OPTgen::access(std::size_t set_idx, uint64_t address)
{
    return access(set_idx, address, 0).opt_hit;
}

OPTgen::AccessResult OPTgen::access(std::size_t set_idx,
                                    uint64_t address,
                                    uint64_t current_pc)
{
    if (set_idx >= sets_.size()) {
        throw std::out_of_range(
            "OPTgen set index out of range");
    }

    SetState& set = sets_[set_idx];

    AccessResult result{
        false,
        false,
        0
    };

    /*
     * Check whether this address has appeared before.
     *
     * The previous PC must be captured BEFORE updating last_pc,
     * because this is the PC that should receive the predictor
     * training for the current reuse.
     */
    const auto previous = set.last_access.find(address);

    if (previous != set.last_access.end()) {
        result.has_previous = true;

        const auto previous_pc = set.last_pc.find(address);

        if (previous_pc != set.last_pc.end()) {
            result.previous_pc = previous_pc->second;
        }

        /*
         * Calculate the reuse distance in accesses to this set.
         */
        const std::size_t distance =
            set.timestamp - previous->second;

        /*
         * If the previous occurrence is outside the tracked
         * history window, OPTgen cannot reconstruct the complete
         * reuse interval.
         */
        if (distance <= set.occupancy.size()) {
            const std::size_t start =
                set.occupancy.size() - distance;

            bool can_keep = true;

            /*
             * OPT can retain the line only if every point in its
             * reuse interval has occupancy below associativity.
             */
            for (std::size_t i = start;
                 i < set.occupancy.size();
                 ++i) {
                if (set.occupancy[i] >=
                    static_cast<int>(associativity_)) {
                    can_keep = false;
                    break;
                }
            }

            if (can_keep) {
                result.opt_hit = true;

                /*
                 * This line remains live under OPT, so increase
                 * occupancy across its reuse interval.
                 */
                for (std::size_t i = start;
                     i < set.occupancy.size();
                     ++i) {
                    ++set.occupancy[i];
                }
            }
        }
    }

    /*
     * Every access advances the history, including:
     *
     *   - real cache hits
     *   - real cache misses
     *   - first references
     *
     * A new access starts with zero occupancy.
     */
    set.occupancy.push_back(0);

    if (set.occupancy.size() > history_length_) {
        set.occupancy.pop_front();
    }

    /*
     * Record the current access for the next occurrence.
     */
    set.last_access[address] = set.timestamp;
    set.last_pc[address] = current_pc;

    ++set.timestamp;

    /*
     * Remove entries that are definitely too old to participate
     * in the tracked history.
     *
     * This does not affect decisions for entries still inside
     * the history window.
     */
    if (set.last_access.size() > history_length_ * 2) {
        for (auto it = set.last_access.begin();
             it != set.last_access.end();) {

            if (set.timestamp - it->second >
                history_length_) {

                set.last_pc.erase(it->first);
                it = set.last_access.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    return result;
}