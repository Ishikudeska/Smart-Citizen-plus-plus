#include "engine/crypto/Aes128.h"
#include "engine/crypto/Sha256.h"

#include <QTest>

#include <string>
#include <vector>

using namespace engine::crypto;

namespace {

std::vector<std::uint8_t> fromHex(std::string_view hex)
{
    std::vector<std::uint8_t> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2)
        out.push_back(static_cast<std::uint8_t>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    return out;
}

std::string toHex(std::span<const std::uint8_t> bytes)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    for (std::uint8_t b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 15]);
    }
    return out;
}

std::string sha256Hex(std::string_view text)
{
    return Sha256::toHex(Sha256::hash(std::span(reinterpret_cast<const std::uint8_t *>(text.data()), text.size())));
}

} // namespace

class TestCrypto : public QObject
{
    Q_OBJECT

private slots:
    // FIPS-197 appendix C.1.
    void aesBlockVector()
    {
        const auto key = fromHex("000102030405060708090a0b0c0d0e0f");
        const auto plain = fromHex("00112233445566778899aabbccddeeff");
        Aes128 aes(std::span<const std::uint8_t, 16>(key.data(), 16));
        std::uint8_t out[16];
        aes.encryptBlock(plain.data(), out);
        QCOMPARE(toHex(out), std::string("69c4e0d86a7b0430d8cdb78070b4c55a"));
        std::uint8_t back[16];
        aes.decryptBlock(out, back);
        QCOMPARE(toHex(back), std::string("00112233445566778899aabbccddeeff"));
    }

    // NIST SP 800-38A F.2.1/F.2.2, decrypted in two chunks to exercise IV chaining.
    void aesCbcVector()
    {
        const auto key = fromHex("2b7e151628aed2a6abf7158809cf4f3c");
        const auto ivBytes = fromHex("000102030405060708090a0b0c0d0e0f");
        const std::string plainHex = "6bc1bee22e409f96e93d7e117393172aae2d8a571e03ac9c9eb76fac45af8e51"
                                     "30c81c46a35ce411e5fbc1191a0a52eff69f2445df4f9b17ad2b417be66c3710";
        const std::string cipherHex = "7649abac8119b246cee98e9b12e9197d5086cb9b507219ee95db113a917678b2"
                                      "73bed6b8e3c1743b7116e69e222295163ff1caa1681fac09120eca307586e1a7";
        Aes128 aes(std::span<const std::uint8_t, 16>(key.data(), 16));

        auto data = fromHex(plainHex);
        Aes128::Block iv;
        std::copy(ivBytes.begin(), ivBytes.end(), iv.begin());
        cbcEncrypt(aes, iv, data);
        QCOMPARE(toHex(data), cipherHex);

        std::copy(ivBytes.begin(), ivBytes.end(), iv.begin());
        cbcDecrypt(aes, iv, std::span(data).first(32));
        cbcDecrypt(aes, iv, std::span(data).subspan(32));
        QCOMPARE(toHex(data), plainHex);
    }

    // FIPS 180-4 examples.
    void sha256Vectors()
    {
        QCOMPARE(sha256Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
        QCOMPARE(sha256Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        QCOMPARE(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
                 std::string("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    }

    void sha256Incremental()
    {
        const std::string million(1000000, 'a');
        Sha256 h;
        const auto *p = reinterpret_cast<const std::uint8_t *>(million.data());
        for (std::size_t i = 0; i < million.size(); i += 777)
            h.update(std::span(p + i, std::min<std::size_t>(777, million.size() - i)));
        QCOMPARE(Sha256::toHex(h.finish()),
                 std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
    }
};

QTEST_APPLESS_MAIN(TestCrypto)
#include "tst_crypto.moc"
