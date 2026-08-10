#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>

namespace neoeng::core {

[[nodiscard]] inline bool parse_fuzz_iteration_count(
    int argc,
    char** argv,
    std::size_t default_value,
    std::size_t maximum_value,
    std::string_view program,
    std::size_t& output) {
    if (argc == 1) {
        output = default_value;
        return true;
    }
    if (argc != 2 || argv == nullptr || argv[1] == nullptr) {
        std::cerr << program << ": expected exactly one positive iteration count\n";
        return false;
    }
    const std::string_view text(argv[1]);
    std::uint64_t parsed{};
    const auto [end, error] = std::from_chars(
        text.data(), text.data() + text.size(), parsed, 10);
    if (error != std::errc{} || end != text.data() + text.size()
        || parsed == 0U || parsed > maximum_value
        || parsed > std::numeric_limits<std::size_t>::max()) {
        std::cerr << program << ": iteration count must be a decimal integer in [1, "
                  << maximum_value << "]\n";
        return false;
    }
    output = static_cast<std::size_t>(parsed);
    return true;
} // parse_fuzz_iteration_count
} // namespace neoeng::core
