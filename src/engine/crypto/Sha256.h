#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace engine::crypto {

// SHA-256 (FIPS 180-4). P4K entries carry a SHA-256 of their stored bytes in
// CIG's 0x5003 extra field; this verifies them.
class Sha256
{
public:
    using Digest = std::array<std::uint8_t, 32>;

    Sha256();
    void update(std::span<const std::uint8_t> data);
    Digest finish();

    static Digest hash(std::span<const std::uint8_t> data);
    static std::string toHex(const Digest &digest);

private:
    void compress(const std::uint8_t *block);

    std::array<std::uint32_t, 8> state_{};
    std::array<std::uint8_t, 64> buffer_{};
    std::size_t buffered_ = 0;
    std::uint64_t length_ = 0;
};

} // namespace engine::crypto
