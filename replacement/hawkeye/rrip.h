#ifndef HAWKEYE_RRIP_H
#define HAWKEYE_RRIP_H

#include <cstddef>
#include <vector>

enum class Classification {
    CACHE_FRIENDLY,
    CACHE_AVERSE
};

// Apply Hawkeye's RRIP update policy to one way.
void update_rrpv(std::vector<int>& rrpv,
                 std::size_t way,
                 Classification cls,
                 bool is_hit);

// Find a victim according to Hawkeye's RRIP policy.
// May age the set before returning a victim.
std::size_t find_victim(std::vector<int>& rrpv);

#endif