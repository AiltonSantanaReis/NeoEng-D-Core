#ifndef NEOENG_DCORE_HOST_H
#define NEOENG_DCORE_HOST_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(NEOENG_DCORE_HOST_SDK_SHARED)
#  if defined(NEOENG_DCORE_HOST_SDK_BUILD)
#    define NEOENG_DCORE_HOST_API __declspec(dllexport)
#  else
#    define NEOENG_DCORE_HOST_API __declspec(dllimport)
#  endif
#else
#  define NEOENG_DCORE_HOST_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define NEOENG_DCORE_HOST_ABI_MAJOR UINT16_C(1)
#define NEOENG_DCORE_HOST_ABI_MINOR UINT16_C(0)
#define NEOENG_DCORE_RUNTIME_VERSION_MAJOR UINT16_C(1)
#define NEOENG_DCORE_RUNTIME_VERSION_MINOR UINT16_C(8)
#define NEOENG_DCORE_RUNTIME_VERSION_PATCH UINT16_C(0)
#define NEOENG_DCORE_SHA256_BYTES UINT32_C(32)

typedef struct neoeng_dcore_host neoeng_dcore_host;
typedef uint32_t neoeng_dcore_status;
typedef uint32_t neoeng_dcore_snapshot_strategy;
typedef uint32_t neoeng_dcore_fault_kind;
typedef uint32_t neoeng_dcore_recovery_acknowledgement;

#define NEOENG_DCORE_STATUS_OK UINT32_C(0)
#define NEOENG_DCORE_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define NEOENG_DCORE_STATUS_ABI_MISMATCH UINT32_C(2)
#define NEOENG_DCORE_STATUS_WRONG_THREAD UINT32_C(3)
#define NEOENG_DCORE_STATUS_INVALID_STATE UINT32_C(4)
#define NEOENG_DCORE_STATUS_NOT_FOUND UINT32_C(5)
#define NEOENG_DCORE_STATUS_BUFFER_TOO_SMALL UINT32_C(6)
#define NEOENG_DCORE_STATUS_RECOVERY_REQUIRED UINT32_C(7)
#define NEOENG_DCORE_STATUS_OUT_OF_MEMORY UINT32_C(8)
#define NEOENG_DCORE_STATUS_NUMERIC_OVERFLOW UINT32_C(9)
#define NEOENG_DCORE_STATUS_INTERNAL_ERROR UINT32_C(255)

#define NEOENG_DCORE_SNAPSHOT_FULL_COPY UINT32_C(0)
#define NEOENG_DCORE_SNAPSHOT_DELTA_LOG UINT32_C(1)
#define NEOENG_DCORE_SNAPSHOT_PAGED_COPY_ON_WRITE UINT32_C(2)
#define NEOENG_DCORE_SNAPSHOT_PERSISTENT_CHUNK_TREE UINT32_C(3)
#define NEOENG_DCORE_SNAPSHOT_COMPONENT_SOA UINT32_C(4)
#define NEOENG_DCORE_SNAPSHOT_HYBRID_ADAPTIVE UINT32_C(5)

#define NEOENG_DCORE_FAULT_NONE UINT32_C(0)
#define NEOENG_DCORE_FAULT_DEVICE_LOST UINT32_C(1)
#define NEOENG_DCORE_FAULT_IO_STALL UINT32_C(2)
#define NEOENG_DCORE_FAULT_NETWORK_UNAVAILABLE UINT32_C(3)
#define NEOENG_DCORE_FAULT_MALFORMED_PACKET UINT32_C(4)
#define NEOENG_DCORE_FAULT_OUT_OF_MEMORY UINT32_C(5)

#define NEOENG_DCORE_RECOVERY_ACK_RETRY_LATER UINT32_C(0)
#define NEOENG_DCORE_RECOVERY_ACK_DEVICE_RESTORED UINT32_C(1)
#define NEOENG_DCORE_RECOVERY_ACK_DEPENDENCY_RESTORED UINT32_C(2)
#define NEOENG_DCORE_RECOVERY_ACK_RESOURCES_RECOVERED UINT32_C(3)
#define NEOENG_DCORE_RECOVERY_ACK_ORIGIN_RESET UINT32_C(4)
#define NEOENG_DCORE_RECOVERY_ACK_CHECKPOINT_RESTORED UINT32_C(5)

#define NEOENG_DCORE_RECOVERY_STATUS_HEALTHY UINT32_C(0x00000000)
#define NEOENG_DCORE_RECOVERY_STATUS_INPUT_DROPPED UINT32_C(0xDC010001)
#define NEOENG_DCORE_RECOVERY_STATUS_INPUT_QUARANTINED UINT32_C(0xDC010002)
#define NEOENG_DCORE_RECOVERY_STATUS_DEVICE_LOST UINT32_C(0xDC020001)
#define NEOENG_DCORE_RECOVERY_STATUS_IO_STALL UINT32_C(0xDC020002)
#define NEOENG_DCORE_RECOVERY_STATUS_NETWORK_UNAVAILABLE UINT32_C(0xDC020003)
#define NEOENG_DCORE_RECOVERY_STATUS_RESOURCE_PRESSURE UINT32_C(0xDC030001)
#define NEOENG_DCORE_RECOVERY_STATUS_CHECKPOINT_RESTORE_REQUIRED UINT32_C(0xDC030002)
#define NEOENG_DCORE_RECOVERY_STATUS_SAFE_HALT UINT32_C(0xDC03FFFF)

#define NEOENG_DCORE_HOST_DIRECTIVE_CONTINUE_SIMULATION UINT32_C(0)
#define NEOENG_DCORE_HOST_DIRECTIVE_DROP_INPUT UINT32_C(1)
#define NEOENG_DCORE_HOST_DIRECTIVE_QUARANTINE_ORIGIN UINT32_C(2)
#define NEOENG_DCORE_HOST_DIRECTIVE_CONTINUE_HEADLESS UINT32_C(3)
#define NEOENG_DCORE_HOST_DIRECTIVE_REUSE_LAST_CONFIRMED_INPUT UINT32_C(4)
#define NEOENG_DCORE_HOST_DIRECTIVE_PAUSE_SIMULATION UINT32_C(5)
#define NEOENG_DCORE_HOST_DIRECTIVE_RESTORE_CHECKPOINT UINT32_C(6)
#define NEOENG_DCORE_HOST_DIRECTIVE_DISABLE_TELEMETRY_AND_PAUSE UINT32_C(7)
#define NEOENG_DCORE_HOST_DIRECTIVE_HALT_SIMULATION UINT32_C(8)

#define NEOENG_DCORE_RECOVERY_MODE_NORMAL UINT32_C(0)
#define NEOENG_DCORE_RECOVERY_MODE_HEADLESS UINT32_C(1)
#define NEOENG_DCORE_RECOVERY_MODE_REUSING_LAST_CONFIRMED_INPUT UINT32_C(2)
#define NEOENG_DCORE_RECOVERY_MODE_QUARANTINED_INPUT UINT32_C(3)
#define NEOENG_DCORE_RECOVERY_MODE_SAFE_WAIT UINT32_C(4)
#define NEOENG_DCORE_RECOVERY_MODE_ROLLING_BACK_TO_CHECKPOINT UINT32_C(5)
#define NEOENG_DCORE_RECOVERY_MODE_HALTED UINT32_C(6)

#define NEOENG_DCORE_RECOVERY_ACTION_NONE UINT32_C(0)
#define NEOENG_DCORE_RECOVERY_ACTION_DROP_INPUT UINT32_C(1)
#define NEOENG_DCORE_RECOVERY_ACTION_CONTINUE_HEADLESS UINT32_C(2)
#define NEOENG_DCORE_RECOVERY_ACTION_REUSE_LAST_CONFIRMED_INPUT UINT32_C(3)
#define NEOENG_DCORE_RECOVERY_ACTION_QUARANTINE_ORIGIN UINT32_C(4)
#define NEOENG_DCORE_RECOVERY_ACTION_ENTER_SAFE_WAIT UINT32_C(5)
#define NEOENG_DCORE_RECOVERY_ACTION_ROLLBACK_TO_CHECKPOINT UINT32_C(6)
#define NEOENG_DCORE_RECOVERY_ACTION_DISABLE_NONESSENTIAL_TELEMETRY UINT32_C(7)
#define NEOENG_DCORE_RECOVERY_ACTION_HALT_SAFELY UINT32_C(8)

#define NEOENG_DCORE_RECOVERY_ACK_REJECT_NONE UINT32_C(0)
#define NEOENG_DCORE_RECOVERY_ACK_REJECT_NO_PENDING_RECOVERY UINT32_C(1)
#define NEOENG_DCORE_RECOVERY_ACK_REJECT_STALE_GENERATION UINT32_C(2)
#define NEOENG_DCORE_RECOVERY_ACK_REJECT_INVALID_ACKNOWLEDGEMENT UINT32_C(3)
#define NEOENG_DCORE_RECOVERY_ACK_REJECT_CHECKPOINT_MISMATCH UINT32_C(4)
#define NEOENG_DCORE_RECOVERY_ACK_REJECT_CHECKPOINT_UNAVAILABLE UINT32_C(5)
#define NEOENG_DCORE_RECOVERY_ACK_REJECT_RESOURCE_EXHAUSTED UINT32_C(6)
#define NEOENG_DCORE_RECOVERY_ACK_REJECT_RUNTIME_HALTED UINT32_C(7)

#define NEOENG_DCORE_TRACE_CATEGORY_INPUT UINT32_C(0)
#define NEOENG_DCORE_TRACE_CATEGORY_NETWORK UINT32_C(1)
#define NEOENG_DCORE_TRACE_CATEGORY_SIMULATION UINT32_C(2)
#define NEOENG_DCORE_TRACE_CATEGORY_ROLLBACK UINT32_C(3)
#define NEOENG_DCORE_TRACE_CATEGORY_BUDGET UINT32_C(4)
#define NEOENG_DCORE_TRACE_CATEGORY_RECOVERY UINT32_C(5)
#define NEOENG_DCORE_TRACE_CATEGORY_TOOLING UINT32_C(6)
#define NEOENG_DCORE_TRACE_CATEGORY_EVIDENCE UINT32_C(7)

#define NEOENG_DCORE_TRACE_OUTCOME_ACCEPTED UINT32_C(0)
#define NEOENG_DCORE_TRACE_OUTCOME_REJECTED UINT32_C(1)
#define NEOENG_DCORE_TRACE_OUTCOME_APPLIED UINT32_C(2)
#define NEOENG_DCORE_TRACE_OUTCOME_SKIPPED UINT32_C(3)
#define NEOENG_DCORE_TRACE_OUTCOME_DEGRADED UINT32_C(4)
#define NEOENG_DCORE_TRACE_OUTCOME_RECOVERED UINT32_C(5)
#define NEOENG_DCORE_TRACE_OUTCOME_FAILED UINT32_C(6)

#define NEOENG_DCORE_TRACE_SEVERITY_DEBUG UINT32_C(0)
#define NEOENG_DCORE_TRACE_SEVERITY_INFO UINT32_C(1)
#define NEOENG_DCORE_TRACE_SEVERITY_WARNING UINT32_C(2)
#define NEOENG_DCORE_TRACE_SEVERITY_ERROR UINT32_C(3)
#define NEOENG_DCORE_TRACE_SEVERITY_CRITICAL UINT32_C(4)

#define NEOENG_DCORE_TRACE_SUBSYSTEM_UNKNOWN UINT32_C(0)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_INPUT_PARSER UINT32_C(1)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_NETWORK_GATEWAY UINT32_C(2)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_SESSION UINT32_C(3)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_SIMULATION UINT32_C(4)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_ROLLBACK UINT32_C(5)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_RECOVERY UINT32_C(6)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_EVIDENCE UINT32_C(7)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_VIEW_LAB UINT32_C(8)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_SUPPORT_BUNDLE UINT32_C(9)
#define NEOENG_DCORE_TRACE_SUBSYSTEM_QUALIFICATION UINT32_C(10)

#define NEOENG_DCORE_TRACE_CODE_NONE UINT32_C(0)
#define NEOENG_DCORE_TRACE_CODE_INPUT_AUTHENTICATED UINT32_C(1)
#define NEOENG_DCORE_TRACE_CODE_INPUT_MALFORMED UINT32_C(2)
#define NEOENG_DCORE_TRACE_CODE_INPUT_REPLAY_REJECTED UINT32_C(3)
#define NEOENG_DCORE_TRACE_CODE_INPUT_RATE_LIMITED UINT32_C(4)
#define NEOENG_DCORE_TRACE_CODE_STATE_ADVANCED UINT32_C(5)
#define NEOENG_DCORE_TRACE_CODE_STATE_DIVERGENCE UINT32_C(6)
#define NEOENG_DCORE_TRACE_CODE_ROLLBACK_STARTED UINT32_C(7)
#define NEOENG_DCORE_TRACE_CODE_ROLLBACK_COMPLETED UINT32_C(8)
#define NEOENG_DCORE_TRACE_CODE_BUDGET_SAMPLED UINT32_C(9)
#define NEOENG_DCORE_TRACE_CODE_BUDGET_EXCEEDED UINT32_C(10)
#define NEOENG_DCORE_TRACE_CODE_DEVICE_LOST UINT32_C(11)
#define NEOENG_DCORE_TRACE_CODE_IO_STALL UINT32_C(12)
#define NEOENG_DCORE_TRACE_CODE_OUT_OF_MEMORY UINT32_C(13)
#define NEOENG_DCORE_TRACE_CODE_SAFE_WAIT_ENTERED UINT32_C(14)
#define NEOENG_DCORE_TRACE_CODE_SAFE_ROLLBACK_ENTERED UINT32_C(15)
#define NEOENG_DCORE_TRACE_CODE_HEADLESS_MODE_ENTERED UINT32_C(16)
#define NEOENG_DCORE_TRACE_CODE_RECOVERY_ACKNOWLEDGED UINT32_C(17)
#define NEOENG_DCORE_TRACE_CODE_RECOVERY_ACKNOWLEDGEMENT_REJECTED UINT32_C(18)
#define NEOENG_DCORE_TRACE_CODE_SESSION_ESTABLISHED UINT32_C(19)
#define NEOENG_DCORE_TRACE_CODE_SESSION_REJECTED UINT32_C(20)
#define NEOENG_DCORE_TRACE_CODE_EVIDENCE_CREATED UINT32_C(21)
#define NEOENG_DCORE_TRACE_CODE_EVIDENCE_VERIFICATION_FAILED UINT32_C(22)
#define NEOENG_DCORE_TRACE_CODE_EVIDENCE_CHAIN_BROKEN UINT32_C(23)
#define NEOENG_DCORE_TRACE_CODE_EVIDENCE_SIGNATURE_REJECTED UINT32_C(24)
#define NEOENG_DCORE_TRACE_CODE_MERKLE_PROOF_REJECTED UINT32_C(25)
#define NEOENG_DCORE_TRACE_CODE_DIVERGENCE_LOCALIZED UINT32_C(26)
#define NEOENG_DCORE_TRACE_CODE_SUPPORT_BUNDLE_CREATED UINT32_C(27)
#define NEOENG_DCORE_TRACE_CODE_SUPPORT_BUNDLE_VERIFICATION_FAILED UINT32_C(28)
#define NEOENG_DCORE_TRACE_CODE_VALIDATION_GATE_DEFERRED UINT32_C(29)

#define NEOENG_DCORE_CAPABILITY_STRICT_TRANSITION (UINT64_C(1) << 0)
#define NEOENG_DCORE_CAPABILITY_ROLLBACK (UINT64_C(1) << 1)
#define NEOENG_DCORE_CAPABILITY_STATE_EVIDENCE (UINT64_C(1) << 2)
#define NEOENG_DCORE_CAPABILITY_RECOVERY_CONTRACT (UINT64_C(1) << 3)
#define NEOENG_DCORE_CAPABILITY_TRACE_EXPORT (UINT64_C(1) << 4)

typedef struct neoeng_dcore_version_info {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint16_t runtime_major;
    uint16_t runtime_minor;
    uint16_t runtime_patch;
    uint16_t reserved;
    uint64_t capabilities;
} neoeng_dcore_version_info;

typedef struct neoeng_dcore_host_config {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    uint64_t snapshot_capacity;
    uint32_t snapshot_strategy;
    uint32_t reserved0;
    uint64_t trace_capacity;
    uint64_t safe_checkpoint_interval_frames;
    uint64_t maximum_inputs_per_step;
} neoeng_dcore_host_config;

typedef struct neoeng_dcore_body {
    uint32_t entity_id;
    uint32_t reserved;
    int64_t position_x_raw;
    int64_t position_y_raw;
    int64_t velocity_x_raw;
    int64_t velocity_y_raw;
} neoeng_dcore_body;

typedef struct neoeng_dcore_input {
    uint32_t entity_id;
    uint32_t reserved;
    int64_t acceleration_x_raw;
    int64_t acceleration_y_raw;
} neoeng_dcore_input;

typedef struct neoeng_dcore_state_summary {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t frame;
    uint64_t body_count;
    uint64_t stable_hash;
    uint8_t canonical_sha256[NEOENG_DCORE_SHA256_BYTES];
    uint8_t merkle_root_sha256[NEOENG_DCORE_SHA256_BYTES];
} neoeng_dcore_state_summary;

typedef struct neoeng_dcore_recovery_event {
    uint32_t struct_size;
    uint16_t contract_version;
    uint16_t acknowledgement_required;
    uint64_t generation;
    uint32_t status_code;
    uint32_t directive;
    uint32_t fault;
    uint32_t action;
    uint32_t mode;
    uint32_t consecutive_fault_count;
    uint64_t frame;
    uint64_t correlation_id;
    uint64_t rollback_checkpoint_frame;
} neoeng_dcore_recovery_event;

typedef struct neoeng_dcore_recovery_ack_result {
    uint32_t struct_size;
    uint32_t accepted;
    uint32_t reject_reason;
    uint32_t resulting_mode;
    neoeng_dcore_recovery_event event;
} neoeng_dcore_recovery_ack_result;

typedef struct neoeng_dcore_trace_event {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t correlation_id;
    uint64_t sequence;
    uint64_t frame;
    uint64_t monotonic_time_ns;
    uint32_t category;
    uint32_t outcome;
    uint32_t code;
    uint32_t entity;
    uint32_t component;
    uint32_t subsystem;
    uint32_t severity;
    uint32_t detail_code;
    int64_t measured_value;
    int64_t budget_limit;
    uint64_t subject_token;
    uint64_t related_hash;
} neoeng_dcore_trace_event;

NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_get_version(
    neoeng_dcore_version_info* out_version);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_default_config(
    neoeng_dcore_host_config* out_config);
NEOENG_DCORE_HOST_API const char* neoeng_dcore_status_string(neoeng_dcore_status status);

NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_create(
    uint64_t initial_frame,
    const neoeng_dcore_body* initial_bodies,
    uint64_t body_count,
    const neoeng_dcore_host_config* config,
    neoeng_dcore_host** out_host);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_destroy(
    neoeng_dcore_host* host);

NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_advance(
    neoeng_dcore_host* host,
    const neoeng_dcore_input* inputs,
    uint64_t input_count,
    uint64_t correlation_id,
    uint64_t monotonic_time_ns,
    neoeng_dcore_state_summary* out_state);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_correct_input_and_resimulate(
    neoeng_dcore_host* host,
    uint64_t input_frame,
    const neoeng_dcore_input* corrected_inputs,
    uint64_t input_count,
    uint64_t correlation_id,
    uint64_t monotonic_time_ns,
    uint64_t* out_resimulated_frames,
    neoeng_dcore_state_summary* out_state);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_restore_checkpoint(
    neoeng_dcore_host* host,
    uint64_t frame,
    uint64_t correlation_id,
    uint64_t monotonic_time_ns,
    neoeng_dcore_state_summary* out_state);

NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_get_state_summary(
    const neoeng_dcore_host* host,
    neoeng_dcore_state_summary* out_state);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_copy_bodies(
    const neoeng_dcore_host* host,
    neoeng_dcore_body* out_bodies,
    uint64_t capacity,
    uint64_t* out_required_count);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_copy_traces(
    const neoeng_dcore_host* host,
    neoeng_dcore_trace_event* out_events,
    uint64_t capacity,
    uint64_t* out_required_count);

NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_report_fault(
    neoeng_dcore_host* host,
    neoeng_dcore_fault_kind fault,
    uint64_t correlation_id,
    uint64_t monotonic_time_ns,
    neoeng_dcore_recovery_event* out_event);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_get_pending_recovery(
    const neoeng_dcore_host* host,
    neoeng_dcore_recovery_event* out_event);
NEOENG_DCORE_HOST_API neoeng_dcore_status neoeng_dcore_host_acknowledge_recovery(
    neoeng_dcore_host* host,
    uint64_t generation,
    neoeng_dcore_recovery_acknowledgement acknowledgement,
    uint64_t correlation_id,
    uint64_t restored_checkpoint_frame,
    uint64_t monotonic_time_ns,
    neoeng_dcore_recovery_ack_result* out_result);

#ifdef __cplusplus
}
#endif

#endif
