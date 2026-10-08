// Historical numerical comparison, separate from the final simulation regressions.
// The runner compiles this twice against source differing only in post-payment clamping.
#include "Simulation/RiftSimulation.h"
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>

int main(int argc, char **argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Supply inclusive first and last one-based match ordinals.");
        const unsigned first = unsigned(std::stoul(argv[1])), last = unsigned(std::stoul(argv[2]));
        if (first == 0 || last < first || last > 1000000)
            throw std::runtime_error("Invalid match ordinal range.");
        const char *styles[] = {"beatdown", "aggro", "control", "cycle", "split", "spell_cycle", "counter"};
        unsigned seed = 151515;
        std::cout << std::setprecision(17);
        for (unsigned ordinal = 1; ordinal <= last; ++ordinal) {
            if (ordinal >= first) {
                rift::MatchOptions options;
                options.seed = seed;
                options.aiEnabled = {true, true};
                options.aiStyles = {styles[seed % 7], styles[(seed / 7) % 7]};
                options.decks[0] = rift::BuildAIDeck(options.aiStyles[0], seed ^ 0x72F3U);
                options.decks[1] = rift::BuildAIDeck(options.aiStyles[1], seed ^ 0xBEEFU);
                rift::Match match(options);
                unsigned plays = 0;
                while (match.State().phase != rift::Phase::Finished && match.State().elapsed < 420) {
                    match.Step(.2);
                    for (const auto &event : match.DrainEvents())
                        if (event.type == "card_play") {
                            std::cout << "P," << ordinal << ',' << seed << ',' << ++plays << ','
                                      << event.time << ',' << int(event.team) << ',' << event.cardId << ','
                                      << event.position.x << ',' << event.position.z << ','
                                      << event.aetherBefore << ',' << event.aetherAfter << '\n';
                        }
                }
                const auto &state = match.State();
                if (state.phase != rift::Phase::Finished)
                    throw std::runtime_error("The selected match did not terminate.");
                std::cout << "M," << ordinal << ',' << seed << ',' << state.elapsed << ',' << state.winner
                          << ',' << state.crowns[0] << ',' << state.crowns[1] << ',' << state.spent[0]
                          << ',' << state.spent[1] << ',' << plays << ',' << state.aether[0] << ','
                          << state.aether[1] << '\n';
            }
            seed = seed * 1664525U + 1013904223U;
        }
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
