#include "neoeng/core/crypto_hash.hpp"
#include "neoeng/core/hash.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/core/simulation.hpp"
#include "neoeng/core/snapshot_store.hpp"
#include "neoeng/core/state_evidence.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace neoeng::core;

constexpr std::string_view kBaseSha =
    "98c1042249ced4c1775dddf9a871e29dc6070828";

Fixed integer(std::int64_t value) {
    return Fixed::from_integer(value);
}

Fixed ratio(std::int64_t numerator, std::int64_t denominator) {
    return Fixed::from_ratio(numerator, denominator);
}

WorldState make_initial_world() {
    return {
        .frame = 7U,
        .bodies = {
            Body{
                .id = 1U,
                .position = {
                    .x = integer(2),
                    .y = integer(-3),
                },
                .velocity = {
                    .x = ratio(1, 2),
                    .y = ratio(-1, 4),
                },
            },
            Body{
                .id = 2U,
                .position = {
                    .x = integer(-5),
                    .y = integer(4),
                },
                .velocity = {
                    .x = ratio(3, 4),
                    .y = ratio(1, 8),
                },
            },
        },
    };
}

std::vector<InputCommand> make_primary_inputs() {
    return {
        InputCommand{
            .entity = 2U,
            .acceleration = {
                .x = ratio(-1, 3),
                .y = ratio(2, 5),
            },
        },
        InputCommand{
            .entity = 1U,
            .acceleration = {
                .x = ratio(1, 2),
                .y = ratio(-1, 6),
            },
        },
    };
}

std::vector<InputCommand> make_corrected_inputs() {
    return {
        InputCommand{
            .entity = 1U,
            .acceleration = {
                .x = ratio(1, 4),
                .y = ratio(1, 7),
            },
        },
        InputCommand{
            .entity = 2U,
            .acceleration = {
                .x = ratio(-1, 5),
                .y = ratio(-1, 8),
            },
        },
    };
}

std::string json_string(std::string_view value) {
    std::ostringstream out;
    out << '"';
    for (const char character : value) {
        const unsigned int byte =
            static_cast<unsigned int>(
                static_cast<unsigned char>(character));
        switch (byte) {
        case '\\':
            out << "\\\\";
            break;
        case '"':
            out << "\\\"";
            break;
        case '\n':
            out << "\\n";
            break;
        case '\r':
            out << "\\r";
            break;
        case '\t':
            out << "\\t";
            break;
        default:
            if (byte < 0x20U) {
                static constexpr char kHex[] = "0123456789abcdef";
                out << "\\u00"
                    << kHex[(byte >> 4U) & 0x0FU]
                    << kHex[byte & 0x0FU];
            } else {
                out << static_cast<char>(byte);
            }
            break;
        }
    }
    out << '"';
    return out.str();
}

std::vector<std::uint8_t> to_bytes(std::string_view text) {
    return {
        reinterpret_cast<const std::uint8_t*>(text.data()),
        reinterpret_cast<const std::uint8_t*>(text.data() + text.size()),
    };
}

void write_bytes(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> bytes) {

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error(
            "Cannot write golden artifact: " + path.string());
    }

    out.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));

    if (!out) {
        throw std::runtime_error(
            "Cannot complete golden artifact write: " + path.string());
    }
}

std::vector<std::uint8_t> read_bytes(
    const std::filesystem::path& path) {

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(
            "Cannot read golden artifact: " + path.string());
    }

    const std::vector<char> raw{
        std::istreambuf_iterator<char>{in},
        std::istreambuf_iterator<char>{},
    };

    return {
        reinterpret_cast<const std::uint8_t*>(raw.data()),
        reinterpret_cast<const std::uint8_t*>(raw.data() + raw.size()),
    };
}

void require_equal(
    const std::filesystem::path& path,
    std::span<const std::uint8_t> expected) {

    const auto actual = read_bytes(path);

    if (actual.size() != expected.size()
        || !std::equal(actual.begin(), actual.end(), expected.begin())) {
        throw std::runtime_error(
            "Golden artifact mismatch: " + path.string());
    }
}

struct CorpusArtifacts final {
    std::vector<std::uint8_t> initial_bytes{};
    std::vector<std::uint8_t> transition_bytes{};
    std::vector<std::uint8_t> replay_bytes{};
    std::vector<std::uint8_t> envelope_bytes{};
    std::string corpus_json{};
    std::string summary{};
};

CorpusArtifacts generate() {
    const WorldState initial = make_initial_world();
    const auto inputs = make_primary_inputs();

    const StepResult transition = step_with_dirty(
        initial,
        std::span<const InputCommand>{inputs});

    if (transition.state.frame != initial.frame + 1U) {
        throw std::runtime_error("Unexpected transition frame");
    }

    const std::array<SnapshotStrategy, 6> strategies{
        SnapshotStrategy::FullCopy,
        SnapshotStrategy::DeltaLog,
        SnapshotStrategy::PagedCopyOnWrite,
        SnapshotStrategy::PersistentChunkTree,
        SnapshotStrategy::ComponentSoA,
        SnapshotStrategy::HybridAdaptive,
    };

    std::vector<std::string> strategy_names;
    strategy_names.reserve(strategies.size());

    for (const SnapshotStrategy strategy : strategies) {
        auto store = make_snapshot_store(strategy, 8U);

        DirtySet initial_dirty =
            DirtySet::full(initial.bodies.size());

        store->capture(initial, &initial_dirty);
        store->capture(transition.state, &transition.dirty);

        if (store->restore(initial.frame) != initial) {
            throw std::runtime_error(
                "Snapshot initial restore mismatch: "
                + std::string(to_string(strategy)));
        }

        if (store->restore(transition.state.frame) != transition.state) {
            throw std::runtime_error(
                "Snapshot transition restore mismatch: "
                + std::string(to_string(strategy)));
        }

        strategy_names.emplace_back(to_string(strategy));
    }

    RollbackEngine rollback{
        initial,
        16U,
        SnapshotStrategy::FullCopy,
    };

    rollback.advance(
        std::span<const InputCommand>{inputs});

    const std::vector<InputCommand> no_inputs;
    rollback.advance(
        std::span<const InputCommand>{no_inputs});

    const auto corrected =
        make_corrected_inputs();

    const std::size_t resimulated =
        rollback.correct_input_and_resimulate(
            initial.frame,
            std::span<const InputCommand>{corrected});

    if (resimulated != 2U) {
        throw std::runtime_error(
            "Unexpected rollback replay cardinality");
    }

    const WorldState replayed =
        rollback.state();

    std::string max_frame_exception;
    std::string max_frame_message;

    WorldState maximum =
        initial;

    maximum.frame =
        std::numeric_limits<std::uint64_t>::max();

    try {
        (void)step(
            maximum,
            std::span<const InputCommand>{});

        throw std::runtime_error(
            "Maximum-frame transition unexpectedly accepted");
    } catch (const std::overflow_error& error) {
        max_frame_exception =
            "std::overflow_error";

        max_frame_message =
            error.what();
    }

    if (
        max_frame_exception != "std::overflow_error"
        || max_frame_message != "World frame maximum reached"
    ) {
        throw std::runtime_error(
            "Maximum-frame diagnostic contract mismatch");
    }

    std::string unknown_exception;
    std::string unknown_message;

    const std::vector<InputCommand> unknown_inputs{
        InputCommand{
            .entity = 999U,
            .acceleration = {},
        },
    };

    try {
        (void)step(
            initial,
            std::span<const InputCommand>{unknown_inputs});

        throw std::runtime_error(
            "Unknown-entity transition unexpectedly accepted");
    } catch (const std::out_of_range& error) {
        unknown_exception =
            "std::out_of_range";

        unknown_message =
            error.what();
    }

    if (
        unknown_exception != "std::out_of_range"
        || unknown_message != "Input references unknown EntityId"
    ) {
        throw std::runtime_error(
            "Unknown-entity diagnostic contract mismatch");
    }

    constexpr std::size_t kMerkleChunkBodies = 1U;

    EvidenceChain chain{
        0xC020U,
        "cs020-golden-v1",
        kMerkleChunkBodies,
    };

    const SignedStateEvidence evidence =
        chain.append(
            transition.state,
            0xC020U,
            123456789U);

    const EvidenceVerifyResult accepted =
        verify_state_evidence(
            transition.state,
            evidence,
            kMerkleChunkBodies);

    if (!accepted.accepted()) {
        throw std::runtime_error(
            "Golden state evidence was rejected");
    }

    SignedStateEvidence rejected =
        evidence;

    rejected.envelope.canonical_state_digest[0] ^=
        0x01U;

    rejected.envelope_hash =
        evidence_envelope_hash(rejected.envelope);

    const EvidenceVerifyResult rejected_result =
        verify_state_evidence(
            transition.state,
            rejected,
            kMerkleChunkBodies);

    if (
        rejected_result.reason !=
        EvidenceVerifyReason::CanonicalDigestMismatch
    ) {
        throw std::runtime_error(
            "Golden deterministic evidence rejection mismatch");
    }

    const auto initial_bytes =
        canonical_serialize(initial);

    const auto transition_bytes =
        canonical_serialize(transition.state);

    const auto replay_bytes =
        canonical_serialize(replayed);

    const auto envelope_bytes =
        canonical_evidence_envelope_bytes(
            evidence.envelope);

    const auto initial_sha =
        canonical_state_sha256(initial);

    const auto transition_sha =
        canonical_state_sha256(transition.state);

    const auto replay_sha =
        canonical_state_sha256(replayed);

    const auto initial_merkle =
        state_merkle_sha256(
            initial,
            kMerkleChunkBodies);

    const auto transition_merkle =
        state_merkle_sha256(
            transition.state,
            kMerkleChunkBodies);

    const auto replay_merkle =
        state_merkle_sha256(
            replayed,
            kMerkleChunkBodies);

    std::ostringstream json;

    json
        << "{\n"
        << "  \"schema\": \"neoeng.dcore.golden-corpus.v1\",\n"
        << "  \"corpus_version\": 1,\n"
        << "  \"base_sha\": " << json_string(kBaseSha) << ",\n"
        << "  \"scenario\": \"ev03-transition-rollback-evidence-v1\",\n"
        << "  \"canonical_world_format_version\": "
        << kCanonicalWorldFormatVersion << ",\n"
        << "  \"state_evidence_schema_version\": "
        << kStateEvidenceSchemaVersion << ",\n"
        << "  \"state_merkle_format_version\": "
        << kStateMerkleFormatVersion << ",\n"
        << "  \"merkle_chunk_bodies\": "
        << kMerkleChunkBodies << ",\n"

        << "  \"initial\": {\n"
        << "    \"frame\": " << initial.frame << ",\n"
        << "    \"stable_hash\": "
        << json_string(hash_hex(stable_hash(initial))) << ",\n"
        << "    \"canonical_sha256\": "
        << json_string(sha256_hex(initial_sha)) << ",\n"
        << "    \"merkle_root_sha256\": "
        << json_string(sha256_hex(initial_merkle.root)) << ",\n"
        << "    \"artifact\": \"world_initial.bin\"\n"
        << "  },\n"

        << "  \"after_transition\": {\n"
        << "    \"frame\": " << transition.state.frame << ",\n"
        << "    \"stable_hash\": "
        << json_string(hash_hex(stable_hash(transition.state))) << ",\n"
        << "    \"canonical_sha256\": "
        << json_string(sha256_hex(transition_sha)) << ",\n"
        << "    \"merkle_root_sha256\": "
        << json_string(sha256_hex(transition_merkle.root)) << ",\n"
        << "    \"artifact\": \"world_after_transition.bin\"\n"
        << "  },\n"

        << "  \"rollback_replay\": {\n"
        << "    \"frame\": " << replayed.frame << ",\n"
        << "    \"resimulated_frames\": " << resimulated << ",\n"
        << "    \"stable_hash\": "
        << json_string(hash_hex(stable_hash(replayed))) << ",\n"
        << "    \"canonical_sha256\": "
        << json_string(sha256_hex(replay_sha)) << ",\n"
        << "    \"merkle_root_sha256\": "
        << json_string(sha256_hex(replay_merkle.root)) << ",\n"
        << "    \"artifact\": \"world_after_rollback_replay.bin\"\n"
        << "  },\n"

        << "  \"snapshot_strategies\": [";

    for (std::size_t index = 0U;
         index < strategy_names.size();
         ++index) {

        if (index != 0U) {
            json << ", ";
        }

        json << json_string(strategy_names[index]);
    }

    json
        << "],\n"
        << "  \"snapshot_restore_all_pass\": true,\n"

        << "  \"diagnostics\": {\n"
        << "    \"maximum_frame\": {\n"
        << "      \"exception\": "
        << json_string(max_frame_exception) << ",\n"
        << "      \"message\": "
        << json_string(max_frame_message) << "\n"
        << "    },\n"
        << "    \"unknown_entity\": {\n"
        << "      \"exception\": "
        << json_string(unknown_exception) << ",\n"
        << "      \"message\": "
        << json_string(unknown_message) << "\n"
        << "    }\n"
        << "  },\n"

        << "  \"state_evidence\": {\n"
        << "    \"artifact\": \"evidence_envelope.bin\",\n"
        << "    \"branch_id\": " << evidence.envelope.branch_id << ",\n"
        << "    \"sequence\": " << evidence.envelope.sequence << ",\n"
        << "    \"frame\": " << evidence.envelope.frame << ",\n"
        << "    \"envelope_sha256\": "
        << json_string(sha256_hex(evidence.envelope_hash)) << ",\n"
        << "    \"accepted\": true,\n"
        << "    \"deterministic_rejection_reason\": "
        << json_string(to_string(rejected_result.reason)) << "\n"
        << "  }\n"
        << "}\n";

    CorpusArtifacts result;
    result.initial_bytes =
        initial_bytes;

    result.transition_bytes =
        transition_bytes;

    result.replay_bytes =
        replay_bytes;

    result.envelope_bytes =
        envelope_bytes;

    result.corpus_json =
        json.str();

    result.summary =
        "cs020_golden_corpus=PASS"
        " states=3"
        " snapshots=6"
        " diagnostics=2"
        " evidence=accepted_and_rejected";

    return result;
}

void emit(
    const std::filesystem::path& root,
    const CorpusArtifacts& artifacts) {

    std::filesystem::create_directories(root);

    write_bytes(
        root / "world_initial.bin",
        artifacts.initial_bytes);

    write_bytes(
        root / "world_after_transition.bin",
        artifacts.transition_bytes);

    write_bytes(
        root / "world_after_rollback_replay.bin",
        artifacts.replay_bytes);

    write_bytes(
        root / "evidence_envelope.bin",
        artifacts.envelope_bytes);

    const auto corpus_bytes =
        to_bytes(artifacts.corpus_json);

    write_bytes(
        root / "corpus.json",
        corpus_bytes);
}

void verify(
    const std::filesystem::path& root,
    const CorpusArtifacts& artifacts) {

    require_equal(
        root / "world_initial.bin",
        artifacts.initial_bytes);

    require_equal(
        root / "world_after_transition.bin",
        artifacts.transition_bytes);

    require_equal(
        root / "world_after_rollback_replay.bin",
        artifacts.replay_bytes);

    require_equal(
        root / "evidence_envelope.bin",
        artifacts.envelope_bytes);

    const auto corpus_bytes =
        to_bytes(artifacts.corpus_json);

    require_equal(
        root / "corpus.json",
        corpus_bytes);
}

} // namespace

int main(int argc, char** argv) {
    try {
        const CorpusArtifacts artifacts =
            generate();

        if (
            argc == 3
            && std::string_view{argv[1]} == "--emit"
        ) {
            emit(
                std::filesystem::path{argv[2]},
                artifacts);

            std::cout
                << artifacts.summary
                << " mode=emit\n";

            return 0;
        }

        if (argc == 2) {
            verify(
                std::filesystem::path{argv[1]},
                artifacts);

            std::cout
                << artifacts.summary
                << " mode=verify\n";

            return 0;
        }

        std::cerr
            << "usage: neoeng_golden_corpus_tests "
               "[--emit] <golden-root>\n";

        return 2;
    } catch (const std::exception& error) {
        std::cerr
            << "cs020_golden_corpus=FAIL error="
            << error.what()
            << '\n';

        return 1;
    }
}
