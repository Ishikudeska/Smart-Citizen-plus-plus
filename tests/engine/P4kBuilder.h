#pragma once

// Builds small P4K archives the way CIG lays out Data.p4k: ZIP64 everywhere,
// the fixed 206-byte central-directory extra field (lengths include their
// headers), AES-encrypted ZSTD entries, a "CIG" comment followed by zero
// padding, and a ZIP64 directory size that is 1,024 bytes too large.

#include "engine/crypto/Aes128.h"
#include "engine/crypto/Sha256.h"
#include "engine/p4k/Archive.h"

#include <zlib.h>
#include <zstd.h>

#include <cstdint>
#include <string>
#include <vector>

namespace testing {

struct P4kEntry
{
    std::string name; // written with backslashes, as CIG does
    std::vector<std::uint8_t> content;
    std::uint16_t method = 100;
    bool encrypted = false;
};

inline std::vector<std::uint8_t> bytesOf(std::string_view s)
{
    return {s.begin(), s.end()};
}

class P4kBuilder
{
public:
    void add(P4kEntry entry) { entries_.push_back(std::move(entry)); }

    std::vector<std::uint8_t> build() const
    {
        std::vector<std::uint8_t> out;
        struct Placed
        {
            std::uint64_t offset;
            std::vector<std::uint8_t> stored;
            std::uint32_t crc;
        };
        std::vector<Placed> placed;

        for (const auto &e : entries_) {
            std::vector<std::uint8_t> stored = encode(e);
            const std::uint32_t crc =
                static_cast<std::uint32_t>(crc32(0, e.content.data(), static_cast<uInt>(e.content.size())));
            const std::string name = backslashed(e.name);

            const std::uint64_t offset = out.size();
            put32(out, 0x04034b50);
            put16(out, 45);
            put16(out, 0);
            put16(out, e.method);
            put32(out, 0);
            put32(out, crc);
            put32(out, 0xFFFFFFFF);
            put32(out, 0xFFFFFFFF);
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 20);
            out.insert(out.end(), name.begin(), name.end());
            put16(out, 0x0001);
            put16(out, 16);
            put64(out, e.content.size());
            put64(out, stored.size());
            out.insert(out.end(), stored.begin(), stored.end());
            placed.push_back({offset, std::move(stored), crc});
        }

        const std::uint64_t cdOffset = out.size();
        for (std::size_t i = 0; i < entries_.size(); ++i) {
            const auto &e = entries_[i];
            const auto &p = placed[i];
            const std::string name = backslashed(e.name);
            put32(out, 0x02014b50);
            put16(out, 46);
            put16(out, 45);
            put16(out, 0);
            put16(out, e.method);
            put32(out, 0);
            put32(out, p.crc);
            put32(out, 0xFFFFFFFF);
            put32(out, 0xFFFFFFFF);
            put16(out, static_cast<std::uint16_t>(name.size()));
            put16(out, 206);
            put16(out, 0);
            put16(out, 0xFFFF);
            put16(out, 0);
            put32(out, 0);
            put32(out, 0xFFFFFFFF);
            out.insert(out.end(), name.begin(), name.end());

            // 0x0001: ZIP64 sizes + offset + disk (length 0x20 includes the header)
            put16(out, 0x0001);
            put16(out, 0x20);
            put64(out, e.content.size());
            put64(out, p.stored.size());
            put64(out, p.offset);
            put32(out, 0);
            // 0x5000: 128 opaque bytes
            put16(out, 0x5000);
            put16(out, 0x84);
            out.insert(out.end(), 128, 0xAB);
            // 0x5002: encryption flag
            put16(out, 0x5002);
            put16(out, 6);
            put16(out, e.encrypted ? 1 : 0);
            // 0x5003: SHA-256 of the stored bytes
            put16(out, 0x5003);
            put16(out, 0x24);
            const auto sha = engine::crypto::Sha256::hash(p.stored);
            out.insert(out.end(), sha.begin(), sha.end());
        }
        const std::uint64_t cdSize = out.size() - cdOffset;

        const std::uint64_t zip64Offset = out.size();
        put32(out, 0x06064b50);
        put64(out, 44);
        put16(out, 46);
        put16(out, 45);
        put32(out, 0);
        put32(out, 0);
        put64(out, entries_.size());
        put64(out, entries_.size());
        put64(out, cdSize + 1024); // wrong on purpose, like Data.p4k
        put64(out, cdOffset);

        put32(out, 0x07064b50);
        put32(out, 0);
        put64(out, zip64Offset);
        put32(out, 1);

        put32(out, 0x06054b50);
        put16(out, 0xFFFF);
        put16(out, 0xFFFF);
        put16(out, 0xFFFF);
        put16(out, 0xFFFF);
        put32(out, 0xFFFFFFFF);
        put32(out, 0xFFFFFFFF);
        const std::uint8_t comment[16] = {'C', 'I', 'G', 0, 1, 0, 0, 0x10, 0x9E, 0x40, 0, 0, 0, 0, 0, 0};
        put16(out, sizeof comment);
        out.insert(out.end(), comment, comment + sizeof comment);
        out.insert(out.end(), 828, 0);
        return out;
    }

    // What the archive stores for an entry: compressed, then encrypted.
    static std::vector<std::uint8_t> encode(const P4kEntry &e)
    {
        std::vector<std::uint8_t> data;
        if (e.method == 100) {
            data.resize(ZSTD_compressBound(e.content.size()));
            data.resize(ZSTD_compress(data.data(), data.size(), e.content.data(), e.content.size(), 3));
        } else if (e.method == 8) {
            z_stream zs{};
            deflateInit2(&zs, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            data.resize(deflateBound(&zs, static_cast<uLong>(e.content.size())));
            zs.next_in = const_cast<Bytef *>(e.content.data());
            zs.avail_in = static_cast<uInt>(e.content.size());
            zs.next_out = data.data();
            zs.avail_out = static_cast<uInt>(data.size());
            deflate(&zs, Z_FINISH);
            data.resize(zs.total_out);
            deflateEnd(&zs);
        } else {
            data = e.content;
        }
        if (e.encrypted) {
            data.resize((data.size() + 15) / 16 * 16, 0);
            engine::crypto::Aes128 aes(engine::p4k::kCigKey);
            engine::crypto::Aes128::Block iv{};
            engine::crypto::cbcEncrypt(aes, iv, data);
        }
        return data;
    }

private:
    static std::string backslashed(std::string s)
    {
        for (char &c : s)
            if (c == '/')
                c = '\\';
        return s;
    }
    static void put16(std::vector<std::uint8_t> &o, std::uint64_t v)
    {
        for (int i = 0; i < 2; ++i)
            o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    static void put32(std::vector<std::uint8_t> &o, std::uint64_t v)
    {
        for (int i = 0; i < 4; ++i)
            o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }
    static void put64(std::vector<std::uint8_t> &o, std::uint64_t v)
    {
        for (int i = 0; i < 8; ++i)
            o.push_back(static_cast<std::uint8_t>(v >> (8 * i)));
    }

    std::vector<P4kEntry> entries_;
};

} // namespace testing
