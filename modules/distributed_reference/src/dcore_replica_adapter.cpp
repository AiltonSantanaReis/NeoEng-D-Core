#include "neoeng/dcore_replica_adapter.hpp"

#include "neoeng/core/hash.hpp"
#include "neoeng/core/state_evidence.hpp"

#include <limits>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace neoeng::distributed_reference {
namespace {

template <typename T>
void append_little_endian(std::vector<std::uint8_t>& output, T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t index = 0U; index < sizeof(U); ++index) {
        output.push_back(static_cast<std::uint8_t>(bits & U{0xFF}));
        bits >>= 8U;
    }
}

template <typename T>
[[nodiscard]] bool read_little_endian(
    std::span<const std::uint8_t> input,
    std::size_t& cursor,
    T& value) noexcept {
    if (cursor > input.size() || input.size() - cursor < sizeof(T)) {
        return false;
    }
    using U = std::make_unsigned_t<T>;
    U bits{};
    for (std::size_t index = 0U; index < sizeof(U); ++index) {
        bits |= static_cast<U>(input[cursor++]) << (index * 8U);
    }
    value = static_cast<T>(bits);
    return true;
}

[[nodiscard]] std::vector<std::uint8_t> encode_inputs(
    std::span<const core::InputCommand> inputs) {
    std::vector<std::uint8_t> output;
    constexpr std::size_t command_bytes =
        sizeof(core::EntityId) + 2U * sizeof(core::Fixed::rep);
    output.reserve(8U + inputs.size() * command_bytes);
    append_little_endian(output, kDCoreInputCorrectionVersion);
    append_little_endian(output, std::uint16_t{0U});
    append_little_endian(output, static_cast<std::uint32_t>(inputs.size()));
    for (const core::InputCommand& input : inputs) {
        append_little_endian(output, input.entity);
        append_little_endian(output, input.acceleration.x.raw());
        append_little_endian(output, input.acceleration.y.raw());
    }
    return output;
}

[[nodiscard]] bool decode_inputs(
    std::span<const std::uint8_t> bytes,
    std::vector<core::InputCommand>& output) {
    constexpr std::size_t header_bytes = 8U;
    constexpr std::size_t command_bytes =
        sizeof(core::EntityId) + 2U * sizeof(core::Fixed::rep);
    if (bytes.size() < header_bytes) {
        return false;
    }
    std::size_t cursor{};
    std::uint16_t version{};
    std::uint16_t reserved{};
    std::uint32_t count{};
    if (!read_little_endian(bytes, cursor, version)
        || !read_little_endian(bytes, cursor, reserved)
        || !read_little_endian(bytes, cursor, count)
        || version != kDCoreInputCorrectionVersion
        || reserved != 0U
        || count > (bytes.size() - header_bytes) / command_bytes
        || bytes.size() != header_bytes + static_cast<std::size_t>(count) * command_bytes) {
        return false;
    }
    output.clear();
    output.reserve(count);
    for (std::uint32_t index = 0U; index < count; ++index) {
        core::InputCommand command{};
        core::Fixed::rep acceleration_x{};
        core::Fixed::rep acceleration_y{};
        if (!read_little_endian(bytes, cursor, command.entity)
            || !read_little_endian(bytes, cursor, acceleration_x)
            || !read_little_endian(bytes, cursor, acceleration_y)
            || command.entity == 0U) {
            return false;
        }
        command.acceleration.x = core::Fixed::from_raw(acceleration_x);
        command.acceleration.y = core::Fixed::from_raw(acceleration_y);
        output.push_back(command);
    }
    return true;
}

} // namespace

DCoreReplicaAdapter::DCoreReplicaAdapter(
    core::WorldState initial,
    std::size_t snapshot_capacity,
    core::SnapshotStrategy strategy)
    : engine_(std::move(initial), snapshot_capacity, strategy),
      correction_retention_(snapshot_capacity > 0U ? snapshot_capacity - 1U : 0U) {}

void DCoreReplicaAdapter::advance(std::span<const core::InputCommand> inputs) {
    input_history_[engine_.state().frame] =
        std::vector<core::InputCommand>(inputs.begin(), inputs.end());
    engine_.advance(inputs);
    while (input_history_.size() > correction_retention_) {
        input_history_.erase(input_history_.begin());
    }
}

core::StateDivergenceReport DCoreReplicaAdapter::diagnose_against(
    const DCoreReplicaAdapter& other,
    core::CorrelationId correlation_id) const {
    return core::diagnose_state_divergence(
        engine_.state(), other.engine_.state(), correlation_id);
}

StateFingerprint DCoreReplicaAdapter::fingerprint() const {
    return {
        .schema_id = kDCoreBodyInputSchemaId,
        .frame = engine_.state().frame,
        .canonical_digest = core::canonical_state_sha256(engine_.state()),
    };
}

std::vector<std::uint8_t> DCoreReplicaAdapter::export_authoritative_correction(
    std::uint64_t input_frame) const {
    const auto found = input_history_.find(input_frame);
    if (found == input_history_.end()) {
        return {};
    }
    return encode_inputs(found->second);
}

ReplicaStatus DCoreReplicaAdapter::apply_authoritative_correction(
    std::uint64_t input_frame,
    std::span<const std::uint8_t> correction) noexcept {
    try {
        std::vector<core::InputCommand> inputs;
        if (!decode_inputs(correction, inputs)) {
            return ReplicaStatus::InvalidCorrection;
        }
        (void)engine_.correct_input_and_resimulate(input_frame, inputs);
        input_history_[input_frame] = std::move(inputs);
        return ReplicaStatus::Accepted;
    } catch (const std::out_of_range&) {
        return ReplicaStatus::CorrectionOutsideRetention;
    } catch (const std::bad_alloc&) {
        return ReplicaStatus::ResourceExhausted;
    } catch (...) {
        return ReplicaStatus::InvalidCorrection;
    }
}

} // namespace neoeng::distributed_reference
