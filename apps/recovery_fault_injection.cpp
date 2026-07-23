#include "neoeng/core/recovery.hpp"

#include <array>
#include <cstdlib>
#include <iostream>

int main() {
    using namespace neoeng::core;
    RecoveryController controller;
    controller.mark_safe_checkpoint(120U);
    const std::array<FaultKind, 7> faults{
        FaultKind::DeviceLost,
        FaultKind::IoStall,
        FaultKind::IoStall,
        FaultKind::MalformedPacket,
        FaultKind::OutOfMemory,
        FaultKind::OutOfMemory,
        FaultKind::NetworkUnavailable,
    };
    std::uint64_t frame = 128U;
    for (const FaultKind fault : faults) {
        const std::uint64_t correlation = frame;
        const RecoverySignal signal = controller.report_fault(fault, frame, correlation);
        ++frame;
        std::cout << "fault=" << to_string(signal.fault)
                  << " action=" << to_string(signal.action)
                  << " mode=" << to_string(signal.mode)
                  << " frame=" << signal.frame
                  << " checkpoint=" << signal.rollback_checkpoint_frame << '\n';
    }
    return EXIT_SUCCESS;
}
