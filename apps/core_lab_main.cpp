#include "neoeng/core/hash.hpp"
#include "neoeng/core/interactive_rollback.hpp"

#include <iostream>

using namespace neoeng::core;

int main() {
    WorldState initial{
        .frame = 0,
        .bodies = {
            Body{
                .id = 1,
                .position = {Fixed::from_integer(-1), {}},
                .velocity = {Fixed::from_integer(60), {}},
            },
            Body{
                .id = 2,
                .position = {Fixed::from_integer(1), {}},
                .velocity = {Fixed::from_integer(-60), {}},
            },
        },
    };

    InteractiveRollbackConfig config;
    config.capacity = 300U;
    config.page_size = 64U;
    config.contacts.half_extent = Fixed::from_ratio(1, 2);
    config.contacts.cell_size = Fixed::from_integer(2);
    config.contacts.broadphase_mode = ContactBroadphaseMode::SweptCellRaster;

    InteractiveRollbackEngine engine(initial, config);
    for (std::size_t frame = 0U; frame < 8U; ++frame) engine.advance({});

    const WorldState final = engine.materialized_state();
    const InteractiveRollbackStats stats = engine.stats();
    std::cout << "NeoEng Deterministic Core Lab v0.11\n"
              << "frame=" << final.frame << '\n'
              << "body[1].position.x=" << final.bodies[0].position.x.to_double() << '\n'
              << "body[2].position.x=" << final.bodies[1].position.x.to_double() << '\n'
              << "canonical_hash=" << hash_hex(stable_hash(final)) << '\n'
              << "contacts_solved=" << stats.contacts_solved << '\n'
              << "retained_frames=" << stats.retained_frames << '\n'
              << "arena_allocations=" << stats.arena.allocations << '\n';
    return 0;
}
