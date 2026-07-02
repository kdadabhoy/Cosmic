// Framing.h — COBS + CRC16 codec tests. Pure functions, no engine state.

#include <doctest.h>

#include "serial/Framing.h"

#include <vector>
#include <cstring>

using namespace Cosmic::Framing;

TEST_CASE("Crc16Ccitt matches the CCITT-FALSE check value")
{
    const uint8_t check[] = { '1','2','3','4','5','6','7','8','9' };
    CHECK(Crc16Ccitt(check, sizeof(check)) == 0x29B1);
}

TEST_CASE("COBS round-trips payloads containing zero bytes")
{
    const std::vector<std::vector<uint8_t>> payloads = {
        { 0x11 },
        { 0x00 },
        { 0x00, 0x00 },
        { 0x11, 0x22, 0x00, 0x33 },
        { 0x11, 0x00, 0x00, 0x00, 0x22 },
    };

    for (const auto& payload : payloads)
    {
        std::vector<uint8_t> encoded(CobsMaxEncoded(payload.size()));
        const size_t encLen = CobsEncode(payload.data(), payload.size(), encoded.data(), encoded.size());
        REQUIRE(encLen > 0);

        // COBS guarantees no interior zero bytes.
        for (size_t i = 0; i < encLen; ++i)
            CHECK(encoded[i] != 0);

        std::vector<uint8_t> decoded(encLen);
        const size_t decLen = CobsDecode(encoded.data(), encLen, decoded.data(), decoded.size());
        REQUIRE(decLen == payload.size());
        CHECK(std::memcmp(decoded.data(), payload.data(), decLen) == 0);
    }
}

TEST_CASE("COBS round-trips a long payload (group boundary at 254 bytes)")
{
    std::vector<uint8_t> payload(300);
    for (size_t i = 0; i < payload.size(); ++i)
        payload[i] = (uint8_t)((i % 255) + 1); // nonzero ramp crossing the 254 group size

    std::vector<uint8_t> encoded(CobsMaxEncoded(payload.size()));
    const size_t encLen = CobsEncode(payload.data(), payload.size(), encoded.data(), encoded.size());
    REQUIRE(encLen > payload.size()); // at least one extra code byte

    std::vector<uint8_t> decoded(encLen);
    const size_t decLen = CobsDecode(encoded.data(), encLen, decoded.data(), decoded.size());
    REQUIRE(decLen == payload.size());
    CHECK(std::memcmp(decoded.data(), payload.data(), decLen) == 0);
}

TEST_CASE("CobsDecode rejects malformed input")
{
    uint8_t out[64];

    // Embedded zero (a delimiter can never appear inside a frame body).
    const uint8_t embeddedZero[] = { 0x02, 0x11, 0x00, 0x02, 0x22 };
    CHECK(CobsDecode(embeddedZero, sizeof(embeddedZero), out, sizeof(out)) == 0);

    // Truncated group: code byte promises more data than remains.
    const uint8_t truncated[] = { 0x05, 0x11, 0x22 };
    CHECK(CobsDecode(truncated, sizeof(truncated), out, sizeof(out)) == 0);
}

TEST_CASE("EncodeFrame/DecodeFrame round-trip with CRC protection")
{
    const uint8_t payload[] = { 0xDE, 0xAD, 0x00, 0xBE, 0xEF, 0x00, 0x00, 0x42 };

    uint8_t frame[MaxFrameSize(sizeof(payload))];
    const size_t frameLen = EncodeFrame(payload, sizeof(payload), frame, sizeof(frame));
    REQUIRE(frameLen > 0);

    // The one and only zero byte is the trailing delimiter.
    CHECK(frame[frameLen - 1] == 0x00);
    for (size_t i = 0; i < frameLen - 1; ++i)
        CHECK(frame[i] != 0x00);

    // Decode the span between delimiters (i.e. without the trailing zero).
    uint8_t decoded[64];
    const size_t decLen = DecodeFrame(frame, frameLen - 1, decoded, sizeof(decoded));
    REQUIRE(decLen == sizeof(payload));
    CHECK(std::memcmp(decoded, payload, decLen) == 0);
}

TEST_CASE("DecodeFrame rejects a corrupted frame")
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };

    uint8_t frame[MaxFrameSize(sizeof(payload))];
    const size_t frameLen = EncodeFrame(payload, sizeof(payload), frame, sizeof(frame));
    REQUIRE(frameLen > 0);

    // Flip one payload bit — CRC must catch it.
    frame[1] ^= 0x10;

    uint8_t decoded[64];
    CHECK(DecodeFrame(frame, frameLen - 1, decoded, sizeof(decoded)) == 0);
}

TEST_CASE("EncodeFrame reports insufficient output capacity as 0")
{
    const uint8_t payload[] = { 0x01, 0x02, 0x03, 0x04 };
    uint8_t tiny[3];
    CHECK(EncodeFrame(payload, sizeof(payload), tiny, sizeof(tiny)) == 0);
}
