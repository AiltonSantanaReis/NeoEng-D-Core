#include "neoeng/core/fixed_raa_microkernel.hpp"

#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1
            ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
            : 1'024U;
        if (iterations == 0U) throw std::invalid_argument("iteration count must be positive");
        for (const std::size_t terms : {8U, 12U, 16U}) {
            const auto probe = neoeng::core::run_fixed_raa_operation_probe(iterations, terms);
            const auto kernel = neoeng::core::run_fixed_raa_microkernel({
                .bodies = 128U,
                .steps = 8U,
                .maximum_terms = terms,
                .monte_carlo_samples = 2'048U,
                .timing_repetitions = 2U,
                .seed = 0x2800280028002800ULL + terms,
            });
            if (kernel.empirical_violations != 0U || probe.compressions == 0U
                || probe.rounding_guard_raw == 0U
                || probe.disjoint_product_cases != iterations) {
                throw std::runtime_error("RAA sanitizer probe invariant failed");
            }
            std::cout << "terms=" << terms << " probe_hash=0x" << std::hex << std::uppercase
                      << probe.hash << " kernel_hash=0x" << kernel.hash << std::dec
                      << " compressions=" << probe.compressions
                      << " disjoint_cases=" << probe.disjoint_product_cases << '\n';
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "v0.28 focused RAA sanitizer probe failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
