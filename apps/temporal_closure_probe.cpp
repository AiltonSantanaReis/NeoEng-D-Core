#include "neoeng/core/temporal_contract.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

using namespace neoeng::core;

[[nodiscard]] Sha256Digest digest(std::string_view value) {
    return sha256(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(value.data()), value.size()));
}

class ProbeExecutor final : public ExternalEffectExecutor {
public:
    std::size_t commits{};
    std::size_t compensations{};

    ExternalEffectApplyResult commit(const ExternalEffectIntent&) override {
        ++commits;
        return ExternalEffectApplyResult::Applied;
    }

    ExternalEffectApplyResult compensate(const ExternalEffectIntent&) override {
        ++compensations;
        return ExternalEffectApplyResult::Applied;
    }
};

struct ProbeDirectory final {
    std::filesystem::path path;

    ProbeDirectory()
        : path(std::filesystem::temp_directory_path()
            / ("neoeng-cs012-probe-" + std::to_string(
                std::chrono::high_resolution_clock::now()
                    .time_since_epoch().count()))) {}

    ~ProbeDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

[[nodiscard]] std::size_t parse_count(
    int argc,
    char** argv,
    int index,
    std::size_t fallback) {
    if (argc <= index) {
        return fallback;
    }
    const std::string argument(argv[index]);
    std::size_t consumed{};
    const unsigned long long value = std::stoull(argument, &consumed, 10);
    if (consumed != argument.size() || value == 0U
        || value > 100'000U) {
        throw std::invalid_argument("probe count must be in [1, 100000]");
    }
    return static_cast<std::size_t>(value);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::size_t durable_segments = parse_count(argc, argv, 1, 512U);
        const std::size_t effect_count = parse_count(argc, argv, 2, 4'096U);
        ProbeDirectory temporary;
        DurableTimelineRecorder recorder(temporary.path);
        if (!recorder.recover().accepted()) {
            throw std::runtime_error("durable recorder recovery failed");
        }
        for (std::size_t index = 0U; index < durable_segments; ++index) {
            const std::string timeline =
                "{\"segment\":" + std::to_string(index)
                + ",\"seed\":12012}";
            const std::string evidence =
                "{\"frame\":" + std::to_string(index)
                + ",\"producer\":\"cs012-probe\"}";
            const DurableRecorderResult result = recorder.append({
                .branch_id = index < durable_segments / 2U ? 1U : 2U,
                .first_frame = static_cast<std::uint64_t>(index),
                .last_frame = static_cast<std::uint64_t>(index),
                .timeline_json = timeline,
                .evidence_json = evidence,
            });
            if (!result.accepted()) {
                throw std::runtime_error(
                    std::string("durable append failed: ")
                    + to_string(result.reason));
            }
        }
        const DurableRecorderResult verified =
            verify_durable_timeline_directory(temporary.path);
        if (!verified.accepted()) {
            throw std::runtime_error("independent durable verification failed");
        }

        ExternalEffectLedger ledger;
        ProbeExecutor executor;
        for (std::size_t index = 0U; index < effect_count; ++index) {
            const std::string key = "probe:" + std::to_string(index);
            const ExternalEffectIntent intent{
                .idempotency_key = key,
                .kind = "probe.effect",
                .frame = static_cast<std::uint64_t>(index),
                .payload_sha256 = digest(key),
                .compensation_supported = index % 2U == 0U,
            };
            if (!ledger.prepare(intent).accepted()) {
                throw std::runtime_error("effect preparation failed");
            }
            if (!ledger.commit(
                    key, static_cast<std::uint64_t>(index), executor).accepted()) {
                throw std::runtime_error("effect commit failed");
            }
            if (intent.compensation_supported
                && !ledger.compensate(key, executor).accepted()) {
                throw std::runtime_error("effect compensation failed");
            }
        }
        if (executor.commits != effect_count
            || executor.compensations != (effect_count + 1U) / 2U) {
            throw std::runtime_error("external effect execution count mismatch");
        }

        std::cout
            << "{\"schema\":\"" << kTemporalClosureSchema
            << "\",\"status\":\"passed\""
            << ",\"seed\":12012"
            << ",\"durable_segments\":" << durable_segments
            << ",\"durable_head_sha256\":\""
            << sha256_hex(verified.record_sha256) << '"'
            << ",\"external_effects\":" << effect_count
            << ",\"commits\":" << executor.commits
            << ",\"compensations\":" << executor.compensations
            << ",\"canonical_fields\":"
            << canonical_world_v1_fields().size()
            << ",\"mandatory_paths\":"
            << mandatory_operational_paths_v1().size()
            << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr
            << "{\"schema\":\"" << kTemporalClosureSchema
            << "\",\"status\":\"failed\",\"error\":\""
            << error.what() << "\"}\n";
        return 1;
    }
}
