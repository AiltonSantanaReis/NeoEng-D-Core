#include "neoeng/core/recovery_contract.hpp"

#include <cstdlib>
#include <iostream>

using namespace neoeng::core;

int main() {
    RecoveryController controller({
        .continue_headless_after_device_loss = false,
        .maximum_consecutive_io_stalls = 1U,
        .maximum_consecutive_network_gaps = 1U,
        .malformed_packets_before_quarantine = 2U,
    });
    controller.mark_safe_checkpoint(40U);
    RecoveryHostBridge bridge;

    const RecoveryContractEvent event = bridge.publish(
        controller.report_fault(FaultKind::DeviceLost, 41U, 0xD00DU));
    const RecoveryAckResult stale = bridge.acknowledge(
        controller, event.generation + 1U, RecoveryAcknowledgement::DeviceRestored, 41U);
    const RecoveryAckResult restored = bridge.acknowledge(
        controller, event.generation, RecoveryAcknowledgement::DeviceRestored, 41U);

    std::cout << recovery_contract_json(event) << '\n'
              << "stale_ack=" << to_string(stale.reason) << '\n'
              << "restored=" << (restored.accepted ? "true" : "false") << '\n'
              << "mode=" << to_string(controller.mode()) << '\n';
    return stale.reason == RecoveryAckRejectReason::StaleGeneration
        && restored.accepted && controller.mode() == RecoveryMode::Normal
        ? EXIT_SUCCESS
        : EXIT_FAILURE;
}
