#pragma once

#include "neoeng/core/island_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace neoeng::core {

enum class GeneralProjectionMethod : std::uint8_t {
    DykstraCoordinate,
    ActiveSetCoordinate,
    ProjectedConjugateGradient,
    CertifiedAuto,
};

[[nodiscard]] const char* to_string(GeneralProjectionMethod method) noexcept;

struct GeneralProjectionConfig final {
    std::size_t maximum_iterations{256U};
    std::size_t certification_tolerance_raw{1U};
    std::size_t pcg_restart_interval{16U};
};

struct GeneralProjectionResiduals final {
    std::uint64_t primal_linf_raw{};
    std::uint64_t dual_linf_raw{};
    std::uint64_t stationarity_linf_raw{};
    std::uint64_t complementarity_linf_scaled_raw{};
    std::uint64_t projected_dual_linf_raw{};
    std::uint64_t quantization_linf_raw{};
    bool certified{};
};

struct GeneralProjectionStats final {
    GeneralProjectionMethod method{GeneralProjectionMethod::DykstraCoordinate};
    std::uint64_t islands_processed{};
    std::uint64_t contacts_processed{};
    std::uint64_t iterations{};
    std::uint64_t coordinate_updates{};
    std::uint64_t active_edges_peak{};
    std::uint64_t pcg_restarts{};
    std::uint64_t total_order_reductions{};
    std::uint64_t iterative_fallbacks{};
    std::uint64_t warm_attempts{};
    std::uint64_t warm_exact_accepts{};
    std::uint64_t warm_rejects{};
    GeneralProjectionResiduals residuals{};
};

class GeneralProjectionScratch final {
public:
    GeneralProjectionScratch(std::size_t maximum_bodies, std::size_t maximum_contacts);

    [[nodiscard]] std::size_t maximum_bodies() const noexcept { return maximum_bodies_; }
    [[nodiscard]] std::size_t maximum_contacts() const noexcept { return maximum_contacts_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

public:
    // Internal fixed-capacity buffers exposed to allocation-free solver helpers.
    std::size_t maximum_bodies_{};
    std::size_t maximum_contacts_{};

    std::vector<Fixed::rep> original_{};
    std::vector<Fixed::rep> cold_values_{};
    std::vector<Fixed::rep> warm_values_{};
    std::vector<Fixed::rep> body_accumulator_{};
    std::vector<Fixed::rep> stationarity_{};

    std::vector<Fixed::rep> lambda_{};
    std::vector<Fixed::rep> cold_lambda_{};
    std::vector<Fixed::rep> warm_lambda_{};
    std::vector<Fixed::rep> residual_{};
    std::vector<Fixed::rep> direction_{};
    std::vector<Fixed::rep> matrix_direction_{};
    std::vector<std::uint8_t> active_{};
    std::vector<std::size_t> block_begin_{};
    std::vector<std::size_t> block_end_{};
    std::vector<WideInteger> block_sum_{};
    std::vector<std::size_t> chain_edge_{};

    std::vector<WideInteger> original_guarded_{};
    std::vector<WideInteger> cold_values_guarded_{};
    std::vector<WideInteger> warm_values_guarded_{};
    std::vector<WideInteger> body_accumulator_guarded_{};
    std::vector<WideInteger> stationarity_guarded_{};
    std::vector<WideInteger> cold_lambda_guarded_{};
    std::vector<WideInteger> warm_lambda_guarded_{};

    friend class GeneralProjectionWarmStart;
    friend GeneralProjectionStats project_general_contact_islands(
        std::span<Fixed::rep>,
        std::span<const SweptContact>,
        const ContactIslandWorkspace&,
        GeneralProjectionMethod,
        GeneralProjectionConfig,
        GeneralProjectionScratch&,
        class GeneralProjectionWarmStart*);
};

class GeneralProjectionWarmStart final {
public:
    explicit GeneralProjectionWarmStart(std::size_t maximum_contacts);

    void clear() noexcept;
    [[nodiscard]] std::size_t capacity() const noexcept { return maximum_contacts_; }
    [[nodiscard]] std::size_t size() const noexcept { return contact_count_; }
    [[nodiscard]] std::size_t reserved_bytes() const noexcept;

public:
    // Internal fixed-capacity cache exposed to canonical validation helpers.
    std::size_t maximum_contacts_{};
    std::size_t contact_count_{};
    bool initialized_{};
    std::vector<std::size_t> first_{};
    std::vector<std::size_t> second_{};
    std::vector<ContactAxis> axis_{};
    std::vector<WideInteger> lambda_guarded_{};

    friend GeneralProjectionStats project_general_contact_islands(
        std::span<Fixed::rep>,
        std::span<const SweptContact>,
        const ContactIslandWorkspace&,
        GeneralProjectionMethod,
        GeneralProjectionConfig,
        GeneralProjectionScratch&,
        GeneralProjectionWarmStart*);
};

// Computes the canonical equal-weight projection for all classified contact islands.
// Cold output is authoritative. When a warm cache is supplied, a second candidate is
// evaluated and accepted only if it is bit-identical to the cold result; otherwise the
// cold result is retained and the cache is refreshed from the canonical cold dual.
[[nodiscard]] GeneralProjectionStats project_general_contact_islands(
    std::span<Fixed::rep> values,
    std::span<const SweptContact> contacts,
    const ContactIslandWorkspace& workspace,
    GeneralProjectionMethod method,
    GeneralProjectionConfig config,
    GeneralProjectionScratch& scratch,
    GeneralProjectionWarmStart* warm_start = nullptr);

} // namespace neoeng::core
