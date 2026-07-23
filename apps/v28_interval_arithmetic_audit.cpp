#include "neoeng/core/fixed_raa_selective_lab.hpp"

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
[[nodiscard]] std::size_t parse_positive(const char* text) {
    const std::string value(text);
    std::size_t consumed = 0U;
    const auto parsed = std::stoull(value, &consumed, 10);
    if (consumed != value.size() || parsed == 0U) {
        throw std::invalid_argument("invalid interval audit case count");
    }
    return static_cast<std::size_t>(parsed);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t cases = argc > 1 ? parse_positive(argv[1]) : 1'000'000U;
        const auto result = neoeng::core::run_fixed_raa_interval_arithmetic_audit(cases);
        std::cout << "cases=" << result.cases
                  << " corner_checks=" << result.multiplication_corner_checks
                  << " violations=" << result.violations
                  << " hash=0x" << std::hex << std::uppercase << result.hash << std::dec
                  << '\n';
        return result.violations == 0U ? EXIT_SUCCESS : 2;
    } catch (const std::exception& exception) {
        std::cerr << "v0.28 interval arithmetic audit failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
