#include "neoeng/dcore_host.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {

[[noreturn]] void fail(const char* message) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}

void require(bool condition, const char* message) {
    if (!condition) {
        fail(message);
    }
}

neoeng_dcore_host_config default_config() {
    neoeng_dcore_host_config config{};
    require(neoeng_dcore_host_default_config(&config) == NEOENG_DCORE_STATUS_OK,
            "default config");
    return config;
}

neoeng_dcore_host* create_host() {
    const std::array<neoeng_dcore_body, 2> bodies{{
        {1U, 0U, 0, 0, 0, 0},
        {2U, 0U, 0, 0, 0, 0},
    }};
    neoeng_dcore_host* host = nullptr;
    const auto config = default_config();
    require(neoeng_dcore_host_create(0U, bodies.data(), bodies.size(), &config, &host)
                == NEOENG_DCORE_STATUS_OK,
            "create valid host");
    require(host != nullptr, "host pointer");
    return host;
}

void test_version_and_creation_guards() {
    neoeng_dcore_version_info version{};
    require(neoeng_dcore_host_get_version(&version) == NEOENG_DCORE_STATUS_OK,
            "get version");
    require(version.abi_major == 1U && version.abi_minor == 0U,
            "ABI version");
    require(version.runtime_major == 1U && version.runtime_minor == 14U,
            "runtime version");
    require(version.runtime_patch == 1U, "runtime patch version");
    require((version.capabilities & NEOENG_DCORE_CAPABILITY_ROLLBACK) != 0U,
            "rollback capability");

    auto config = default_config();
    config.abi_major = 2U;
    neoeng_dcore_host* host = nullptr;
    require(neoeng_dcore_host_create(0U, nullptr, 0U, &config, &host)
                == NEOENG_DCORE_STATUS_ABI_MISMATCH,
            "reject ABI major mismatch");
    require(host == nullptr, "mismatch does not create host");

    config = default_config();
    config.reserved0 = 1U;
    require(neoeng_dcore_host_create(0U, nullptr, 0U, &config, &host)
                == NEOENG_DCORE_STATUS_INVALID_ARGUMENT,
            "reject nonzero config reserved field");

    config = default_config();
    const neoeng_dcore_body reserved_body{1U, 1U, 0, 0, 0, 0};
    require(neoeng_dcore_host_create(0U, &reserved_body, 1U, &config, &host)
                == NEOENG_DCORE_STATUS_INVALID_ARGUMENT,
            "reject nonzero body reserved field");

    const std::array<neoeng_dcore_body, 2> unsorted{{
        {2U, 0U, 0, 0, 0, 0},
        {1U, 0U, 0, 0, 0, 0},
    }};
    require(neoeng_dcore_host_create(0U, unsorted.data(), unsorted.size(), &config, &host)
                == NEOENG_DCORE_STATUS_INVALID_STATE,
            "reject unsorted canonical state");
}

void test_state_rollback_and_buffers() {
    neoeng_dcore_host* empty_host = nullptr;
    neoeng_dcore_host_config empty_config = default_config();
    require(neoeng_dcore_host_create(
                0U, nullptr, 0U, &empty_config, &empty_host)
                == NEOENG_DCORE_STATUS_OK,
            "create empty host");
    std::uint64_t empty_count = 1U;
    require(neoeng_dcore_host_copy_bodies(
                empty_host, nullptr, 0U, &empty_count)
                == NEOENG_DCORE_STATUS_OK
            && empty_count == 0U,
            "empty body copy accepts null output");
    empty_count = 1U;
    require(neoeng_dcore_host_copy_traces(
                empty_host, nullptr, 0U, &empty_count)
                == NEOENG_DCORE_STATUS_OK
            && empty_count == 0U,
            "empty trace copy accepts null output");
    require(neoeng_dcore_host_destroy(empty_host) == NEOENG_DCORE_STATUS_OK,
            "destroy empty host");

    neoeng_dcore_host* host = create_host();
    neoeng_dcore_state_summary initial{};
    require(neoeng_dcore_host_get_state_summary(host, &initial) == NEOENG_DCORE_STATUS_OK,
            "initial state summary");
    require(initial.frame == 0U && initial.body_count == 2U, "initial state values");

    std::uint64_t required = 0U;
    require(neoeng_dcore_host_copy_bodies(host, nullptr, 0U, &required)
                == NEOENG_DCORE_STATUS_BUFFER_TOO_SMALL,
            "body sizing query");
    require(required == 2U, "body sizing count");

    const neoeng_dcore_input input{
        1U, 0U, INT64_C(4294967296), 0,
    };
    neoeng_dcore_state_summary first{};
    require(neoeng_dcore_host_advance(host, &input, 1U, 10U, 100U, &first)
                == NEOENG_DCORE_STATUS_OK,
            "first advance");
    require(first.frame == 1U && first.stable_hash != initial.stable_hash,
            "first advance state");

    neoeng_dcore_state_summary second{};
    require(neoeng_dcore_host_advance(host, nullptr, 0U, 11U, 200U, &second)
                == NEOENG_DCORE_STATUS_OK,
            "second advance");
    require(second.frame == 2U, "second frame");

    const neoeng_dcore_input corrected{
        1U, 0U, INT64_C(8589934592), 0,
    };
    std::uint64_t resimulated = 0U;
    neoeng_dcore_state_summary corrected_state{};
    require(neoeng_dcore_host_correct_input_and_resimulate(
                host, 0U, &corrected, 1U, 12U, 300U, &resimulated, &corrected_state)
                == NEOENG_DCORE_STATUS_OK,
            "correct and resimulate");
    require(resimulated == 2U && corrected_state.frame == 2U,
            "resimulation count");
    require(corrected_state.stable_hash != second.stable_hash,
            "correction changes deterministic state");

    neoeng_dcore_state_summary restored{};
    require(neoeng_dcore_host_restore_checkpoint(host, 0U, 13U, 400U, &restored)
                == NEOENG_DCORE_STATUS_OK,
            "restore checkpoint");
    require(restored.frame == 0U && restored.stable_hash == initial.stable_hash,
            "restored canonical state");

    std::array<neoeng_dcore_body, 2> bodies{};
    require(neoeng_dcore_host_copy_bodies(host, bodies.data(), bodies.size(), &required)
                == NEOENG_DCORE_STATUS_OK,
            "copy bodies");
    require(bodies[0].position_x_raw == 0 && bodies[0].velocity_x_raw == 0,
            "restored body values");

    std::uint64_t trace_count = 0U;
    require(neoeng_dcore_host_copy_traces(host, nullptr, 0U, &trace_count)
                == NEOENG_DCORE_STATUS_BUFFER_TOO_SMALL,
            "trace sizing query");
    require(trace_count >= 6U, "trace events recorded");

    require(neoeng_dcore_host_destroy(host) == NEOENG_DCORE_STATUS_OK,
            "destroy host");
}

void test_recovery_contract() {
    neoeng_dcore_host* host = create_host();
    neoeng_dcore_state_summary state{};
    require(neoeng_dcore_host_advance(host, nullptr, 0U, 20U, 0U, &state)
                == NEOENG_DCORE_STATUS_OK,
            "advance before recovery");

    neoeng_dcore_recovery_event first{};
    require(neoeng_dcore_host_report_fault(
                host, NEOENG_DCORE_FAULT_OUT_OF_MEMORY, 21U, 0U, &first)
                == NEOENG_DCORE_STATUS_OK,
            "first OOM report");
    require(first.acknowledgement_required == 1U, "first OOM requires ack");
    require(neoeng_dcore_host_advance(host, nullptr, 0U, 22U, 0U, &state)
                == NEOENG_DCORE_STATUS_RECOVERY_REQUIRED,
            "safe wait blocks advance");

    neoeng_dcore_recovery_ack_result first_ack{};
    require(neoeng_dcore_host_acknowledge_recovery(
                host, first.generation, NEOENG_DCORE_RECOVERY_ACK_RESOURCES_RECOVERED,
                23U, 0U, 0U, &first_ack) == NEOENG_DCORE_STATUS_OK,
            "first OOM acknowledgement call");
    require(first_ack.accepted == 1U, "first OOM acknowledgement accepted");

    neoeng_dcore_recovery_event second{};
    require(neoeng_dcore_host_report_fault(
                host, NEOENG_DCORE_FAULT_OUT_OF_MEMORY, 24U, 0U, &second)
                == NEOENG_DCORE_STATUS_OK,
            "second OOM report");
    require(second.rollback_checkpoint_frame == 1U, "safe checkpoint frame");

    neoeng_dcore_recovery_ack_result stale{};
    require(neoeng_dcore_host_acknowledge_recovery(
                host, second.generation - 1U,
                NEOENG_DCORE_RECOVERY_ACK_CHECKPOINT_RESTORED,
                25U, second.rollback_checkpoint_frame, 0U, &stale)
                == NEOENG_DCORE_STATUS_OK,
            "stale acknowledgement call");
    require(stale.accepted == 0U, "stale acknowledgement rejected");

    neoeng_dcore_recovery_ack_result second_ack{};
    require(neoeng_dcore_host_acknowledge_recovery(
                host, second.generation,
                NEOENG_DCORE_RECOVERY_ACK_CHECKPOINT_RESTORED,
                26U, second.rollback_checkpoint_frame, 0U, &second_ack)
                == NEOENG_DCORE_STATUS_OK,
            "checkpoint acknowledgement call");
    require(second_ack.accepted == 1U, "checkpoint acknowledgement accepted");

    require(neoeng_dcore_host_destroy(host) == NEOENG_DCORE_STATUS_OK,
            "destroy recovery host");
}

void test_thread_ownership_and_input_guards() {
    neoeng_dcore_host* host = create_host();
    neoeng_dcore_status worker_status = NEOENG_DCORE_STATUS_OK;
    std::thread worker([&] {
        neoeng_dcore_state_summary summary{};
        worker_status = neoeng_dcore_host_get_state_summary(host, &summary);
    });
    worker.join();
    require(worker_status == NEOENG_DCORE_STATUS_WRONG_THREAD,
            "wrong-thread access rejected");

    const neoeng_dcore_input invalid_entity{};
    neoeng_dcore_state_summary state{};
    require(neoeng_dcore_host_advance(host, &invalid_entity, 1U, 30U, 0U, &state)
                == NEOENG_DCORE_STATUS_INVALID_ARGUMENT,
            "invalid input entity rejected");

    const neoeng_dcore_input reserved_input{1U, 1U, 0, 0};
    require(neoeng_dcore_host_advance(host, &reserved_input, 1U, 31U, 0U, &state)
                == NEOENG_DCORE_STATUS_INVALID_ARGUMENT,
            "nonzero input reserved field rejected");

    require(neoeng_dcore_host_destroy(host) == NEOENG_DCORE_STATUS_OK,
            "destroy ownership host");
}

} // namespace

int main() {
    test_version_and_creation_guards();
    test_state_rollback_and_buffers();
    test_recovery_contract();
    test_thread_ownership_and_input_guards();
    std::cout << "host_sdk_tests=PASS abi=1.0 runtime=1.12.0\n";
    return 0;
}
