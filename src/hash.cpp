#include "neoeng/core/hash.hpp"

#include <iomanip>
#include <sstream>
#include <type_traits>

namespace neoeng::core {
namespace {

constexpr std::uint64_t kOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kPrime = 1'099'511'628'211ULL;

template <typename T>
void append_vector_little_endian(std::vector<std::uint8_t>& bytes, T value) {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t index = 0; index < sizeof(U); ++index) {
        bytes.push_back(static_cast<std::uint8_t>(bits & U{0xFF}));
        bits >>= 8U;
    }
}

} // namespace

WorldHashBuilder::WorldHashBuilder(std::uint64_t frame, std::size_t body_count) noexcept
    : hash_(kOffset) {
    append_little_endian(kCanonicalWorldMagic);
    append_little_endian(kCanonicalWorldFormatVersion);
    append_little_endian(std::uint16_t{0});
    append_little_endian(frame);
    append_little_endian(static_cast<std::uint64_t>(body_count));
}

void WorldHashBuilder::append_byte(std::uint8_t byte) noexcept {
    hash_ ^= byte;
    hash_ *= kPrime;
}

template <typename T>
void WorldHashBuilder::append_little_endian(T value) noexcept {
    using U = std::make_unsigned_t<T>;
    U bits = static_cast<U>(value);
    for (std::size_t i = 0; i < sizeof(U); ++i) {
        append_byte(static_cast<std::uint8_t>(bits & U{0xFF}));
        bits >>= 8U;
    }
}

void WorldHashBuilder::append(const Body& body) noexcept {
    append_little_endian(body.id);
    append_little_endian(body.position.x.raw());
    append_little_endian(body.position.y.raw());
    append_little_endian(body.velocity.x.raw());
    append_little_endian(body.velocity.y.raw());
}

std::vector<std::uint8_t> canonical_serialize(const WorldState& state) {
    constexpr std::size_t header_bytes = sizeof(std::uint32_t) + sizeof(std::uint16_t)
        + sizeof(std::uint16_t) + sizeof(std::uint64_t) + sizeof(std::uint64_t);
    constexpr std::size_t body_bytes = sizeof(EntityId) + 4U * sizeof(Fixed::rep);
    std::vector<std::uint8_t> bytes;
    bytes.reserve(header_bytes + state.bodies.size() * body_bytes);
    append_vector_little_endian(bytes, kCanonicalWorldMagic);
    append_vector_little_endian(bytes, kCanonicalWorldFormatVersion);
    append_vector_little_endian(bytes, std::uint16_t{0});
    append_vector_little_endian(bytes, state.frame);
    append_vector_little_endian(bytes, static_cast<std::uint64_t>(state.bodies.size()));
    for (const Body& body : state.bodies) {
        append_vector_little_endian(bytes, body.id);
        append_vector_little_endian(bytes, body.position.x.raw());
        append_vector_little_endian(bytes, body.position.y.raw());
        append_vector_little_endian(bytes, body.velocity.x.raw());
        append_vector_little_endian(bytes, body.velocity.y.raw());
    }
    return bytes;
}

std::uint64_t stable_hash(const WorldState& state) noexcept {
    WorldHashBuilder builder(state.frame, state.bodies.size());
    for (const Body& body : state.bodies) {
        builder.append(body);
    }
    return builder.value();
}

std::string hash_hex(std::uint64_t value) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::uppercase << std::setw(16)
           << std::setfill('0') << value;
    return stream.str();
}

} // namespace neoeng::core
