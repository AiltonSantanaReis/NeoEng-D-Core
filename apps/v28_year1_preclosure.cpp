#include "neoeng/core/hash.hpp"
#include "neoeng/core/rollback.hpp"
#include "neoeng/core/simulation.hpp"
#include "neoeng/core/year1_contract.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#if defined(__linux__)
#include <sys/resource.h>
#include <unistd.h>
#endif

using namespace neoeng::core;

namespace {
constexpr std::uint32_t kReplayMagic = kYear1ReplayMagic;
constexpr std::uint16_t kReplayVersion = kYear1ReplaySchemaVersion;
constexpr std::uint16_t kLegacyReplayVersion = kYear1LegacyReplaySchemaVersion;
constexpr std::uint64_t kSeed = 0x28060000A11CE001ULL;

struct ReplayHeader {
    std::uint32_t magic{kReplayMagic};
    std::uint16_t version{kReplayVersion};
    std::uint16_t header_bytes{32};
    std::uint32_t tick_hz{60};
    std::uint32_t body_count{64};
    std::uint64_t tick_count{};
    std::uint64_t seed{kSeed};
};

struct LegacyReplayHeader {
    std::uint32_t magic{kReplayMagic};
    std::uint16_t version{kLegacyReplayVersion};
    std::uint16_t reserved{};
    std::uint32_t body_count{64};
    std::uint64_t tick_count{};
    std::uint64_t seed{kSeed};
};

struct EncodedCommand {
    std::uint32_t entity{};
    std::int32_t ax{};
    std::int32_t ay{};
};

std::uint64_t rng_next(std::uint64_t& state) noexcept {
    state ^= state >> 12U;
    state ^= state << 25U;
    state ^= state >> 27U;
    return state * 2'685'821'657'736'338'717ULL;
}

WorldState make_world(std::uint32_t count = 64) {
    WorldState world{};
    world.bodies.reserve(count);
    for (std::uint32_t id = 1; id <= count; ++id) {
        world.bodies.push_back(Body{
            .id = id,
            .position = {Fixed::from_integer(static_cast<std::int64_t>(id % 16U)),
                         Fixed::from_integer(static_cast<std::int64_t>(id / 16U))},
            .velocity = {}});
    }
    return world;
}

std::array<InputCommand, 2> commands_for_tick(std::uint64_t tick, std::uint64_t seed = kSeed) {
    std::uint64_t state = seed ^ (tick * 0x9E3779B97F4A7C15ULL);
    std::array<InputCommand, 2> out{};
    for (auto& command : out) {
        const auto sample = rng_next(state);
        command.entity = static_cast<EntityId>((sample % 64U) + 1U);
        const auto ax = static_cast<std::int64_t>((sample >> 11U) % 7U) - 3;
        const auto ay = static_cast<std::int64_t>((sample >> 27U) % 7U) - 3;
        command.acceleration = {Fixed::from_integer(ax), Fixed::from_integer(ay)};
    }
    return out;
}

template <typename T>
void write_le(std::ostream& stream, T value) {
    using U = std::make_unsigned_t<T>;
    U bits{};
    std::memcpy(&bits, &value, sizeof(T));
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        stream.put(static_cast<char>((bits >> (i * 8U)) & U{0xFF}));
    }
    if (!stream) throw std::runtime_error("replay write failed");
}

template <typename T>
T read_le(std::istream& stream) {
    static_assert(std::is_integral_v<T>);
    static_assert(sizeof(T) <= sizeof(std::uint64_t));
    using U = std::make_unsigned_t<T>;
    std::uint64_t bits{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        const int byte = stream.get();
        if (byte == std::char_traits<char>::eof()) throw std::runtime_error("truncated replay");
        bits |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte)) << (i * 8U);
    }
    const U narrowed = static_cast<U>(bits);
    T value{};
    std::memcpy(&value, &narrowed, sizeof(T));
    return value;
}

void write_header(std::ostream& stream, const ReplayHeader& h) {
    write_le(stream, h.magic); write_le(stream, h.version); write_le(stream, h.header_bytes);
    write_le(stream, h.tick_hz); write_le(stream, h.body_count); write_le(stream, h.tick_count); write_le(stream, h.seed);
}

ReplayHeader read_and_migrate_header(std::istream& stream, bool& migrated) {
    const auto magic = read_le<std::uint32_t>(stream);
    const auto version = read_le<std::uint16_t>(stream);
    if (magic != kReplayMagic) throw std::runtime_error("invalid replay magic");
    if (version == kReplayVersion) {
        ReplayHeader h{}; h.magic = magic; h.version = version;
        h.header_bytes = read_le<std::uint16_t>(stream); h.tick_hz = read_le<std::uint32_t>(stream);
        h.body_count = read_le<std::uint32_t>(stream); h.tick_count = read_le<std::uint64_t>(stream); h.seed = read_le<std::uint64_t>(stream);
        if (h.header_bytes != 32 || h.tick_hz == 0 || h.body_count == 0) throw std::runtime_error("invalid v1 replay header");
        migrated = false; return h;
    }
    if (version == kLegacyReplayVersion) {
        (void)read_le<std::uint16_t>(stream);
        ReplayHeader h{}; h.magic = magic; h.version = kReplayVersion; h.header_bytes = 32; h.tick_hz = 60;
        h.body_count = read_le<std::uint32_t>(stream); h.tick_count = read_le<std::uint64_t>(stream); h.seed = read_le<std::uint64_t>(stream);
        migrated = true; return h;
    }
    throw std::runtime_error("unsupported replay version");
}

void write_command(std::ostream& stream, const InputCommand& command) {
    write_le(stream, command.entity);
    write_le(stream, static_cast<std::int32_t>(command.acceleration.x.raw() / Fixed::scale));
    write_le(stream, static_cast<std::int32_t>(command.acceleration.y.raw() / Fixed::scale));
}

InputCommand read_command(std::istream& stream) {
    InputCommand command{};
    command.entity = read_le<std::uint32_t>(stream);
    command.acceleration.x = Fixed::from_integer(read_le<std::int32_t>(stream));
    command.acceleration.y = Fixed::from_integer(read_le<std::int32_t>(stream));
    return command;
}

std::uint64_t rss_kib() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string key;
    while (status >> key) {
        if (key == "VmRSS:") { std::uint64_t value{}; status >> value; return value; }
        std::string rest; std::getline(status, rest);
    }
#endif
    return 0;
}

std::pair<std::uint64_t, std::uint64_t> page_faults() {
#if defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) return {static_cast<std::uint64_t>(usage.ru_minflt), static_cast<std::uint64_t>(usage.ru_majflt)};
#endif
    return {0, 0};
}

int run_replay(const std::filesystem::path& out, std::uint64_t ticks) {
    std::filesystem::create_directories(out);
    const auto replay_path = out / "year1_replay_v1.bin";
    const auto checkpoint_path = out / "year1_replay_checkpoints.csv";
    std::ofstream replay(replay_path, std::ios::binary);
    ReplayHeader header{}; header.tick_count = ticks;
    write_header(replay, header);
    WorldState capture = make_world(header.body_count);
    std::vector<std::pair<std::uint64_t, std::uint64_t>> checkpoints;
    checkpoints.reserve(static_cast<std::size_t>(ticks / 10'000U) + 1U);
    for (std::uint64_t tick = 0; tick < ticks; ++tick) {
        const auto commands = commands_for_tick(tick, header.seed);
        write_le<std::uint8_t>(replay, static_cast<std::uint8_t>(commands.size()));
        for (const auto& command : commands) write_command(replay, command);
        capture = step(capture, commands);
        if ((tick + 1U) % 10'000U == 0U || tick + 1U == ticks) checkpoints.emplace_back(tick + 1U, stable_hash(capture));
    }
    replay.close();

    std::ifstream input(replay_path, std::ios::binary);
    bool migrated = false;
    const auto parsed = read_and_migrate_header(input, migrated);
    WorldState replayed = make_world(parsed.body_count);
    std::size_t checkpoint_index = 0;
    for (std::uint64_t tick = 0; tick < parsed.tick_count; ++tick) {
        const auto count = read_le<std::uint8_t>(input);
        std::vector<InputCommand> commands; commands.reserve(count);
        for (std::uint8_t i = 0; i < count; ++i) commands.push_back(read_command(input));
        replayed = step(replayed, commands);
        if ((tick + 1U) % 10'000U == 0U || tick + 1U == parsed.tick_count) {
            const auto hash = stable_hash(replayed);
            if (checkpoint_index >= checkpoints.size() || checkpoints[checkpoint_index] != std::pair{tick + 1U, hash}) {
                std::cerr << "replay divergence at tick " << (tick + 1U) << '\n'; return 1;
            }
            ++checkpoint_index;
        }
    }
    if (input.peek() != std::char_traits<char>::eof()) { std::cerr << "trailing replay bytes\n"; return 1; }

    // Explicit legacy-header migration test.
    const auto legacy_path = out / "legacy_header_v0.bin";
    { std::ofstream legacy(legacy_path, std::ios::binary); write_le(legacy, kReplayMagic); write_le(legacy, kLegacyReplayVersion); write_le<std::uint16_t>(legacy, 0); write_le<std::uint32_t>(legacy, 64); write_le<std::uint64_t>(legacy, 0); write_le<std::uint64_t>(legacy, kSeed); }
    { std::ifstream legacy(legacy_path, std::ios::binary); bool did_migrate = false; const auto migrated_header = read_and_migrate_header(legacy, did_migrate); if (!did_migrate || migrated_header.version != 1 || migrated_header.tick_hz != 60) return 1; }

    std::ofstream csv(checkpoint_path);
    csv << "tick,hash\n";
    for (const auto& [tick, hash] : checkpoints) csv << tick << ',' << hash_hex(hash) << '\n';
    const auto final_hash = stable_hash(replayed);
    std::cout << "replay_ticks=" << ticks << "\ncheckpoint_count=" << checkpoints.size() << "\nfinal_hash=" << hash_hex(final_hash)
              << "\nschema_version=1\nlegacy_migration=passed\npassed=true\n";
    return 0;
}

int run_history(const std::filesystem::path& out, std::uint64_t frames) {
    std::filesystem::create_directories(out);
    SnapshotStoreConfig config{}; config.strategy = SnapshotStrategy::HybridAdaptive; config.capacity = 300; config.audit_dirty_contract = true;
    RollbackEngine engine(make_world(1024), config);
    const auto rss_start = rss_kib(); const auto faults_start = page_faults();
    std::ofstream samples(out / "history_300_samples.csv"); samples << "frame,rss_kib,minor_faults,major_faults,retained,live_bytes,peak_bytes,hash\n";
    std::uint64_t max_rss = rss_start;
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        const auto commands = commands_for_tick(frame, kSeed ^ 0x4444ULL);
        engine.advance(commands);
        if ((frame + 1U) % 300U == 0U) {
            const auto rss = rss_kib(); const auto faults = page_faults(); const auto stats = engine.snapshots().stats();
            max_rss = std::max(max_rss, rss);
            samples << frame + 1U << ',' << rss << ',' << faults.first << ',' << faults.second << ',' << stats.retained_frames << ','
                    << stats.live_payload_bytes + stats.live_metadata_bytes << ',' << stats.peak_live_payload_bytes + stats.peak_live_metadata_bytes << ','
                    << hash_hex(stable_hash(engine.state())) << '\n';
            if (stats.retained_frames > 300U) { std::cerr << "history capacity exceeded\n"; return 1; }
            const auto restored = engine.snapshots().restore(engine.state().frame - 299U);
            if (restored.frame != engine.state().frame - 299U) { std::cerr << "restore frame mismatch\n"; return 1; }
        }
    }
    const auto rss_end = rss_kib(); const auto faults_end = page_faults(); const auto stats = engine.snapshots().stats();
    std::cout << "history_frames=" << frames << "\ncapacity=300\nretained=" << stats.retained_frames << "\nrss_start_kib=" << rss_start
              << "\nrss_end_kib=" << rss_end << "\nrss_max_kib=" << max_rss << "\nminor_fault_delta=" << faults_end.first - faults_start.first
              << "\nmajor_fault_delta=" << faults_end.second - faults_start.second << "\nretained_input_frames=" << engine.retained_input_frames() << "\nfinal_hash=" << hash_hex(stable_hash(engine.state())) << "\npassed=" << ((engine.retained_input_frames() <= 300U) ? "true" : "false") << "\n";
    return 0;
}

struct Packet { std::uint64_t delivery{}; std::uint64_t frame{}; std::array<InputCommand,2> commands{}; };

int run_network(const std::filesystem::path& out, std::uint64_t ticks, std::uint64_t seed) {
    std::filesystem::create_directories(out);
    RollbackEngine server(make_world(), 512, SnapshotStrategy::HybridAdaptive);
    RollbackEngine client(make_world(), 512, SnapshotStrategy::HybridAdaptive);
    std::multimap<std::uint64_t, Packet> queue;
    std::uint64_t corrected = 0, initial_losses = 0, delivered = 0;
    std::ofstream csv(out / "network_degraded.csv"); csv << "tick,queued,server_hash,client_hash\n";
    constexpr std::uint64_t base_delay = 8; // 133.33 ms at 60 Hz
    for (std::uint64_t tick = 0; tick < ticks; ++tick) {
        const auto commands = commands_for_tick(tick, seed ^ 0x5555ULL);
        server.advance(commands);
        client.advance(std::span<const InputCommand>{}); // deterministic zero-input prediction
        std::uint64_t state = seed ^ tick;
        const auto sample = rng_next(state);
        const bool lost = (sample % 100U) < 15U;
        const std::int64_t jitter = static_cast<std::int64_t>((sample >> 16U) % 7U) - 3;
        std::uint64_t delivery = tick + base_delay + static_cast<std::uint64_t>(std::max<std::int64_t>(jitter, -static_cast<std::int64_t>(base_delay)));
        if (lost) { ++initial_losses; delivery += 12U + ((sample >> 24U) % 5U); }
        queue.emplace(delivery, Packet{delivery, tick, commands});
        auto range = queue.equal_range(tick);
        std::vector<Packet> due;
        for (auto it = range.first; it != range.second; ++it) due.push_back(it->second);
        queue.erase(range.first, range.second);
        std::sort(due.begin(), due.end(), [](const Packet& a, const Packet& b){ return a.frame < b.frame; });
        for (const auto& packet : due) { (void)client.correct_input_and_resimulate(packet.frame, packet.commands); ++corrected; ++delivered; }
        if ((tick + 1U) % 1000U == 0U) csv << tick + 1U << ',' << queue.size() << ',' << hash_hex(stable_hash(server.state())) << ',' << hash_hex(stable_hash(client.state())) << '\n';
    }
    // Drain while both sides advance with zero input so late corrections are within history.
    while (!queue.empty()) {
        const auto current = client.state().frame;
        server.advance(std::span<const InputCommand>{}); client.advance(std::span<const InputCommand>{});
        auto range = queue.equal_range(current);
        std::vector<Packet> due;
        for (auto it = range.first; it != range.second; ++it) due.push_back(it->second);
        queue.erase(range.first, range.second);
        std::sort(due.begin(), due.end(), [](const Packet& a, const Packet& b){ return a.frame < b.frame; });
        for (const auto& packet : due) { (void)client.correct_input_and_resimulate(packet.frame, packet.commands); ++corrected; ++delivered; }
    }
    const auto server_hash = stable_hash(server.state()); const auto client_hash = stable_hash(client.state());
    const bool pass = server.state() == client.state() && server_hash == client_hash && delivered == ticks;
    std::cout << "network_ticks=" << ticks << "\nbase_latency_ms=133.333\ninitial_loss_percent=" << (100.0 * static_cast<double>(initial_losses) / static_cast<double>(ticks))
              << "\njitter_ticks=3\npackets_delivered=" << delivered << "\ncorrections=" << corrected << "\nserver_hash=" << hash_hex(server_hash)
              << "\nclient_hash=" << hash_hex(client_hash) << "\npassed=" << (pass ? "true" : "false") << "\n";
    return pass ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc < 3) { std::cerr << "usage: neoeng_v28_year1_preclosure <replay|history|network> <output-dir> [count]\n"; return 2; }
        const std::string_view mode = argv[1]; const std::filesystem::path out = argv[2];
        if (mode == "replay") return run_replay(out, argc > 3 ? std::stoull(argv[3]) : 1'000'000ULL);
        if (mode == "history") return run_history(out, argc > 3 ? std::stoull(argv[3]) : 90'000ULL);
        if (mode == "network") return run_network(out, argc > 3 ? std::stoull(argv[3]) : 20'000ULL, argc > 4 ? std::stoull(argv[4], nullptr, 0) : kSeed);
        std::cerr << "unknown mode\n"; return 2;
    } catch (const std::exception& error) { std::cerr << "fatal: " << error.what() << '\n'; return 1; }
}
