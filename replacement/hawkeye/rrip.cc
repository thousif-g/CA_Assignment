#include "rrip.h"

#include <algorithm>
#include <stdexcept>

namespace {
constexpr int MAX_RRPV = 7;
constexpr int FRIENDLY_RRPV = 0;
}

void update_rrpv(std::vector<int>& rrpv,
                 std::size_t way,
                 Classification cls,
                 bool is_hit)
{
    if (way >= rrpv.size()) {
        throw std::out_of_range(
            "RRIP way index out of range");
    }

    if (cls == Classification::CACHE_AVERSE) {
        /*
         * Hawkeye always gives cache-averse lines the
         * highest eviction priority.
         *
         * This applies to both hits and misses.
         */
        rrpv[way] = MAX_RRPV;
        return;
    }

    /*
     * Cache-friendly lines are always assigned RRPV 0,
     * regardless of whether the access was a hit or miss.
     */
    rrpv[way] = FRIENDLY_RRPV;

    /*
     * On a cache-friendly miss (insertion), age all the
     * other lines. This preserves relative age among
     * cache-friendly lines and gives the newly inserted
     * line the highest retention priority.
     *
     * The assignment's Table 1 specifies:
     *
     *   if (RRPV < 6)
     *       RRPV++
     */
    if (!is_hit) {
        for (std::size_t i = 0; i < rrpv.size(); ++i) {
            if (i == way) {
                continue;
            }

            /*
             * Do not saturate friendly lines at 7.
             * Hawkeye reserves 7 for cache-averse lines.
             */
            if (rrpv[i] < MAX_RRPV - 1) {
                ++rrpv[i];
            }
        }
    }
}

std::size_t find_victim(std::vector<int>& rrpv)
{
    if (rrpv.empty()) {
        throw std::invalid_argument(
            "Cannot find victim in an empty set");
    }

    /*
     * First prefer an RRPV of 7, which represents a
     * cache-averse line.
     */
    for (std::size_t i = 0; i < rrpv.size(); ++i) {
        if (rrpv[i] == MAX_RRPV) {
            return i;
        }
    }

    /*
     * If no line has RRPV 7, age the set until one does.
     */
    while (true) {
        for (std::size_t i = 0; i < rrpv.size(); ++i) {
            if (rrpv[i] == MAX_RRPV) {
                return i;
            }
        }

        for (int& value : rrpv) {
            if (value < MAX_RRPV) {
                ++value;
            }
        }
    }
}