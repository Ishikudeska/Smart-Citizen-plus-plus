#pragma once

#include <cstdint>
#include <string>

namespace engine::forge {

// Text that .NET Core 3.0+ produces for `$"{value}"` under the invariant
// culture. unforge formats every number this way, and the DataForge cache has
// to match it byte for byte.
//
// Floating point uses the shortest round-trip digits, laid out by
// Number.FormatGeneral: plain decimal unless the decimal exponent exceeds
// max(digits, 9 for float / 17 for double) or the value is below 1e-4, in
// which case it is d.dddE+XX. So 161410900, 1.093429E+09, 1E-05, -0.
void appendSingle(std::string &out, float value);
void appendDouble(std::string &out, double value);

std::string formatSingle(float value);
std::string formatDouble(double value);

// System.Guid in "D" format, lowercase, from DataForge's on-disk byte order:
// bytes [0..1] are c, [2..3] b, [4..7] a, [8..15] k..d (reversed).
void appendGuid(std::string &out, const std::uint8_t *bytes);
std::string formatGuid(const std::uint8_t *bytes);

} // namespace engine::forge
