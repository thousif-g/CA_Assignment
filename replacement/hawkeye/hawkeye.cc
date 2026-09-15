#include "hawkeye.h"
#include <algorithm>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

hawkeye::hawkeye(CACHE* cache)
    : hawkeye(cache,
              cache->NUM_SET,
              cache->NUM_WAY)
{
}

hawkeye::hawkeye(CACHE* cache,
                 long sets,
                 long ways)
    : replacement(cache),
      NUM_SET(static_cast<std::size_t>(sets)),
      NUM_WAY(static_cast<std::size_t>(ways)),
      optgen(NUM_SET, NUM_WAY),
      predictor(8192, 3),
      rrpv(NUM_SET * NUM_WAY, 7)
{
}

long hawkeye::find_victim(uint32_t triggering_cpu,
                          uint64_t instr_id,
                          long set,
                          const champsim::cache_block* current_set,
                          champsim::address ip,
                          champsim::address full_addr,
                          access_type type)
{
    (void)triggering_cpu;
    (void)instr_id;
    (void)ip;
    (void)full_addr;
    (void)type;

    assert(set >= 0);
    assert(static_cast<std::size_t>(set) < NUM_SET);

    const std::size_t begin =
        static_cast<std::size_t>(set) * NUM_WAY;

    /*
     * Always use an invalid way before replacing a valid line.
     */
    for (std::size_t way = 0;
         way < NUM_WAY;
         ++way) {

        if (!current_set[way].valid) {
            return static_cast<long>(way);
        }
    }

    /*
     * Extract the RRPV state for this set.
     */
    std::vector<int> set_rrpv(
        rrpv.begin() +
            static_cast<std::ptrdiff_t>(begin),
        rrpv.begin() +
            static_cast<std::ptrdiff_t>(begin + NUM_WAY));

    /*
     * RRIP selects the Hawkeye victim.
     *
     * find_victim() may age the RRPVs before returning.
     */
    const std::size_t victim =
        ::find_victim(set_rrpv);

    /*
     * Preserve any RRPV aging performed by RRIP.
     */
    for (std::size_t way = 0;
         way < NUM_WAY;
         ++way) {

        rrpv[begin + way] = set_rrpv[way];
    }

    return static_cast<long>(victim);
}

void hawkeye::replacement_cache_fill(
    uint32_t triggering_cpu,
    long set,
    long way,
    champsim::address full_addr,
    champsim::address ip,
    champsim::address victim_addr,
    access_type type)
{
    (void)triggering_cpu;
    (void)full_addr;
    (void)victim_addr;
    (void)type;

    assert(set >= 0);
    assert(way >= 0);

    assert(static_cast<std::size_t>(set) < NUM_SET);
    assert(static_cast<std::size_t>(way) < NUM_WAY);

    const std::size_t index =
        static_cast<std::size_t>(set) * NUM_WAY +
        static_cast<std::size_t>(way);

    /*
     * The predictor is indexed by the PC of the current access.
     *
     * OPTgen processing itself is performed in
     * update_replacement_state(), which is called for every
     * access, including misses.
     *
     * Therefore OPTgen must NOT be called again here.
     */

    const uint64_t current_pc =
        ip.to<uint64_t>();

    /*
     * Classify the newly inserted line using the current
     * load instruction's prediction.
     */
    const Classification cls =
        predictor.predict(current_pc)
            ? Classification::CACHE_FRIENDLY
            : Classification::CACHE_AVERSE;

    /*
     * Apply Hawkeye's insertion policy through RRIP.
     */
    update_rrpv(rrpv,
                index,
                cls,
                false);
}

void hawkeye::update_replacement_state(
    uint32_t triggering_cpu,
    long set,
    long way,
    champsim::address full_addr,
    champsim::address ip,
    champsim::address victim_addr,
    access_type type,
    uint8_t hit)
{
    (void)triggering_cpu;
    (void)victim_addr;
    (void)type;

    assert(set >= 0);
    assert(static_cast<std::size_t>(set) < NUM_SET);

    /*
     * IMPORTANT:
     *
     * OPTgen must see EVERY access.
     *
     * On a miss, ChampSim may pass way == NUM_WAY because
     * there is no resident way associated with the access yet.
     *
     * We therefore process OPTgen BEFORE checking the way.
     */
    const uint64_t address =
        full_addr.to<uint64_t>();

    const uint64_t current_pc =
        ip.to<uint64_t>();

    const OPTgen::AccessResult result =
        optgen.access(
            static_cast<std::size_t>(set),
            address,
            current_pc);

    /*
     * Train only when this address has a previous occurrence.
     *
     * The PC trained is the PC that generated the PREVIOUS
     * occurrence of this address.
     *
     * This works for both:
     *
     *   - real cache hits
     *   - real cache misses after eviction
     *
     * which is why the previous PC is stored inside OPTgen.
     */
    if (result.has_previous) {
        predictor.train(
            result.previous_pc,
            result.opt_hit);
    }

    /*
     * A real miss has no resident way to update.
     *
     * replacement_cache_fill() will subsequently perform the
     * insertion/RRIP operation.
     */
    if (!hit) {
        return;
    }

    /*
     * For a hit, ChampSim must provide a valid resident way.
     */
    assert(way >= 0);
    assert(static_cast<std::size_t>(way) < NUM_WAY);

    const std::size_t index =
        static_cast<std::size_t>(set) * NUM_WAY +
        static_cast<std::size_t>(way);

    /*
     * Replacement classification is based on the CURRENT
     * access PC, not the previous PC.
     */
    const Classification cls =
        predictor.predict(current_pc)
            ? Classification::CACHE_FRIENDLY
            : Classification::CACHE_AVERSE;

    /*
     * Apply Hawkeye's hit update policy.
     */
    update_rrpv(rrpv,
                index,
                cls,
                true);
}