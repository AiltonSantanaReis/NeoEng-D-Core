#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
#endif

#include "neoeng/core/fixed_raa_microkernel.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

#if defined(_WIN32)
#include <malloc.h>
#endif

namespace allocation_probe {
std::atomic<bool> enabled{false};
std::atomic<std::uint64_t> cpp_allocations{0U};
std::atomic<std::uint64_t> cpp_bytes{0U};
std::atomic<std::uint64_t> c_allocations{0U};
std::atomic<std::uint64_t> c_bytes{0U};

void record_cpp(std::size_t size) noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        cpp_allocations.fetch_add(1U, std::memory_order_relaxed);
        cpp_bytes.fetch_add(size, std::memory_order_relaxed);
    }
}

void record_c(std::size_t size) noexcept {
    if (enabled.load(std::memory_order_relaxed)) {
        c_allocations.fetch_add(1U, std::memory_order_relaxed);
        c_bytes.fetch_add(size, std::memory_order_relaxed);
    }
}

void reset() noexcept {
    cpp_allocations.store(0U, std::memory_order_relaxed);
    cpp_bytes.store(0U, std::memory_order_relaxed);
    c_allocations.store(0U, std::memory_order_relaxed);
    c_bytes.store(0U, std::memory_order_relaxed);
}
} // namespace allocation_probe

#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
extern "C" void* __real_malloc(std::size_t);
extern "C" void* __real_calloc(std::size_t, std::size_t);
extern "C" void* __real_realloc(void*, std::size_t);
extern "C" void __real_free(void*);
extern "C" void* __real_aligned_alloc(std::size_t, std::size_t);
extern "C" int __real_posix_memalign(void**, std::size_t, std::size_t);

extern "C" void* __wrap_malloc(std::size_t size) {
    allocation_probe::record_c(size);
    return __real_malloc(size == 0U ? 1U : size);
}
extern "C" void* __wrap_calloc(std::size_t count, std::size_t size) {
    allocation_probe::record_c(count * size);
    return __real_calloc(count, size);
}
extern "C" void* __wrap_realloc(void* pointer, std::size_t size) {
    allocation_probe::record_c(size);
    return __real_realloc(pointer, size);
}
extern "C" void __wrap_free(void* pointer) { __real_free(pointer); }
extern "C" void* __wrap_aligned_alloc(std::size_t alignment, std::size_t size) {
    allocation_probe::record_c(size);
    return __real_aligned_alloc(alignment, size);
}
extern "C" int __wrap_posix_memalign(void** pointer, std::size_t alignment, std::size_t size) {
    allocation_probe::record_c(size);
    return __real_posix_memalign(pointer, alignment, size);
}

void* operator new(std::size_t size) {
    if (void* pointer = __real_malloc(size == 0U ? 1U : size)) {
        allocation_probe::record_cpp(size);
        return pointer;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { __real_free(pointer); }
void operator delete[](void* pointer) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { __real_free(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { __real_free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    void* pointer = nullptr;
    const auto alignment_value = static_cast<std::size_t>(alignment);
    if (__real_posix_memalign(&pointer, alignment_value, size == 0U ? 1U : size) != 0) throw std::bad_alloc();
    allocation_probe::record_cpp(size);
    return pointer;
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::size_t, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::size_t, std::align_val_t) noexcept { __real_free(pointer); }
void operator delete(void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { __real_free(pointer); }
void operator delete[](void* pointer, std::align_val_t, const std::nothrow_t&) noexcept { __real_free(pointer); }
#else
void* operator new(std::size_t size) {
    if (void* pointer = std::malloc(size == 0U ? 1U : size)) {
        allocation_probe::record_cpp(size);
        return pointer;
    }
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void operator delete[](void* pointer, const std::nothrow_t&) noexcept { std::free(pointer); }
void* operator new(std::size_t size, std::align_val_t alignment) {
    const std::size_t alignment_value = static_cast<std::size_t>(alignment);
#if defined(_WIN32)
    void* pointer = _aligned_malloc(size == 0U ? 1U : size, alignment_value);
#else
    const std::size_t requested = size == 0U ? 1U : size;
    const std::size_t rounded = ((requested + alignment_value - 1U) / alignment_value) * alignment_value;
    void* pointer = std::aligned_alloc(alignment_value, rounded);
#endif
    if (pointer == nullptr) throw std::bad_alloc();
    allocation_probe::record_cpp(size);
    return pointer;
}
void* operator new[](std::size_t size, std::align_val_t alignment) { return ::operator new(size, alignment); }
void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new(size, alignment); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
    try { return ::operator new[](size, alignment); } catch (...) { return nullptr; }
}
void operator delete(void* pointer, std::align_val_t) noexcept {
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}
void operator delete[](void* pointer, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept { ::operator delete(pointer, alignment); }
void operator delete(void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept { ::operator delete(pointer, alignment); }
void operator delete[](void* pointer, std::align_val_t alignment, const std::nothrow_t&) noexcept { ::operator delete(pointer, alignment); }
#endif

namespace {
struct CalibrationResult final {
    std::uint64_t cpp_allocations{};
    std::uint64_t c_allocations{};
    bool passed{};
};

volatile std::uintptr_t calibration_sink{};

[[nodiscard]] CalibrationResult calibrate_allocation_probe() {
    allocation_probe::reset();
    allocation_probe::enabled.store(true, std::memory_order_release);
    bool allocations_succeeded = true;
    try {
        auto* cpp_memory = new std::byte[32U];
        cpp_memory[0] = std::byte{0x5A};
        calibration_sink = reinterpret_cast<std::uintptr_t>(cpp_memory);
        delete[] cpp_memory;

        void* aligned_cpp_memory = ::operator new(64U, std::align_val_t{64U});
        calibration_sink = reinterpret_cast<std::uintptr_t>(aligned_cpp_memory);
        ::operator delete(aligned_cpp_memory, std::align_val_t{64U});

#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
        void* malloc_memory = std::malloc(32U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(malloc_memory);
        void* calloc_memory = std::calloc(2U, 16U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(calloc_memory);
        void* realloc_memory = std::malloc(16U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(realloc_memory);
        void* grown_memory = std::realloc(realloc_memory, 48U);
        if (grown_memory != nullptr) realloc_memory = grown_memory;
        calibration_sink = reinterpret_cast<std::uintptr_t>(realloc_memory);
        void* aligned_memory = std::aligned_alloc(64U, 64U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(aligned_memory);
        void* posix_memory = nullptr;
        const int posix_result = ::posix_memalign(&posix_memory, 64U, 64U);
        calibration_sink = reinterpret_cast<std::uintptr_t>(posix_memory);
        allocations_succeeded = malloc_memory != nullptr && calloc_memory != nullptr
            && grown_memory != nullptr && aligned_memory != nullptr
            && posix_result == 0 && posix_memory != nullptr;
        std::free(malloc_memory);
        std::free(calloc_memory);
        std::free(realloc_memory);
        std::free(aligned_memory);
        std::free(posix_memory);
#endif
    } catch (...) {
        allocation_probe::enabled.store(false, std::memory_order_release);
        throw;
    }
    allocation_probe::enabled.store(false, std::memory_order_release);
    const std::uint64_t cpp_count = allocation_probe::cpp_allocations.load(std::memory_order_relaxed);
    const std::uint64_t c_count = allocation_probe::c_allocations.load(std::memory_order_relaxed);
#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
    const bool passed = allocations_succeeded && cpp_count >= 2U && c_count >= 6U;
#else
    const bool passed = allocations_succeeded && cpp_count >= 2U;
#endif
    allocation_probe::reset();
    return {.cpp_allocations = cpp_count, .c_allocations = c_count, .passed = passed};
}

std::size_t parse_positive(const char* text, const char* field) {
    const unsigned long long parsed = std::strtoull(text, nullptr, 10);
    if (parsed == 0ULL || parsed > static_cast<unsigned long long>(std::numeric_limits<std::size_t>::max())) {
        throw std::invalid_argument(field);
    }
    return static_cast<std::size_t>(parsed);
}
} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t iterations = argc > 1 ? parse_positive(argv[1], "invalid iteration count") : 256U;
        const std::size_t maximum_terms = argc > 2 ? parse_positive(argv[2], "invalid maximum term count") : 8U;

        // Warm-up and counter calibration occur outside the observed window.
        (void)neoeng::core::run_fixed_raa_operation_probe(1U, maximum_terms);
        const CalibrationResult calibration = calibrate_allocation_probe();
        if (!calibration.passed) throw std::runtime_error("allocation probe calibration failed");
        allocation_probe::reset();
        allocation_probe::enabled.store(true, std::memory_order_release);
        const auto result = neoeng::core::run_fixed_raa_operation_probe(iterations, maximum_terms);
        allocation_probe::enabled.store(false, std::memory_order_release);

        const std::uint64_t cpp_count = allocation_probe::cpp_allocations.load(std::memory_order_relaxed);
        const std::uint64_t cpp_bytes = allocation_probe::cpp_bytes.load(std::memory_order_relaxed);
        const std::uint64_t c_count = allocation_probe::c_allocations.load(std::memory_order_relaxed);
        const std::uint64_t c_bytes = allocation_probe::c_bytes.load(std::memory_order_relaxed);
#if defined(NEOENG_C_ALLOCATION_PROBE_SUPPORTED)
        constexpr bool c_probe_supported = true;
#else
        constexpr bool c_probe_supported = false;
#endif
        const bool passed = cpp_count == 0U && (!c_probe_supported || c_count == 0U);
        std::cout << "{\n"
                  << "  \"probe\": \"fixed_raa_operations\",\n"
                  << "  \"iterations\": " << result.iterations << ",\n"
                  << "  \"maximum_terms\": " << result.maximum_terms << ",\n"
                  << "  \"compressions\": " << result.compressions << ",\n"
                  << "  \"rounding_guard_raw\": " << result.rounding_guard_raw << ",\n"
                  << "  \"disjoint_product_cases\": " << result.disjoint_product_cases << ",\n"
                  << "  \"cpp_allocations\": " << cpp_count << ",\n"
                  << "  \"cpp_bytes\": " << cpp_bytes << ",\n"
                  << "  \"allocation_probe_calibrated\": true,\n"
                  << "  \"calibration_cpp_allocations\": " << calibration.cpp_allocations << ",\n"
                  << "  \"calibration_c_allocations\": " << calibration.c_allocations << ",\n"
                  << "  \"c_probe_supported\": " << (c_probe_supported ? "true" : "false") << ",\n"
                  << "  \"c_allocations\": " << c_count << ",\n"
                  << "  \"c_bytes\": " << c_bytes << ",\n"
                  << "  \"hash\": \"0x" << std::hex << std::uppercase << result.hash << std::dec << "\",\n"
                  << "  \"passed\": " << (passed ? "true" : "false") << "\n"
                  << "}\n";
        return passed ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& exception) {
        allocation_probe::enabled.store(false, std::memory_order_release);
        std::cerr << "v0.28 RAA allocation probe failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
