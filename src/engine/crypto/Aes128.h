#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace engine::crypto {

// AES-128 block cipher (FIPS-197). Only what the P4K format needs: CBC with
// no padding, decrypting in chunks. Encryption exists for the tests.
class Aes128
{
public:
    using Block = std::array<std::uint8_t, 16>;

    explicit Aes128(std::span<const std::uint8_t, 16> key);

    void encryptBlock(const std::uint8_t *in, std::uint8_t *out) const;
    void decryptBlock(const std::uint8_t *in, std::uint8_t *out) const;

private:
    std::array<std::uint8_t, 176> roundKeys_{};
};

// CBC over `data` in place. `data.size()` must be a multiple of 16. `iv` is
// updated to the chaining value for the next chunk, so a long stream can be
// processed in pieces.
void cbcDecrypt(const Aes128 &aes, Aes128::Block &iv, std::span<std::uint8_t> data);
void cbcEncrypt(const Aes128 &aes, Aes128::Block &iv, std::span<std::uint8_t> data);

} // namespace engine::crypto
