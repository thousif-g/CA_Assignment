#include "../replacement/hawkeye/predictor.h"
#include <iostream>
#include <vector>
#include <utility>
#include <cstdint>

int main() {
    HawkeyePredictor pred;

    // TEST_VECTOR_START
    std::vector<std::pair<uint64_t, bool>> train_events = {
        {0x1000, true},
        {0x1000, true},
        {0x1000, true},
        {0x1000, true},
        {0x2000, false},
        {0x2000, false},
        {0x2000, false}
    };

    std::vector<uint64_t> query_pcs = {
        0x1000,
        0x2000,
        0x3000
    };
    // TEST_VECTOR_END

    for (auto& [pc, opt_hit] : train_events) {
        pred.train(pc, opt_hit);
    }

    for (uint64_t pc : query_pcs) {
        std::cout << std::hex
                  << pc
                  << std::dec
                  << ": counter="
                  << pred.get_counter(pc)
                  << " predict="
                  << pred.predict(pc)
                  << "\n";
    }
}