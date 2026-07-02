#pragma once

// Framing.h
// Last Modified: 7/1/2026

/**
 * ============================================================================
 * COSMIC ENGINE — Binary frame codec (COBS + CRC16)
 * ============================================================================
 *
 * DELIBERATELY FREESTANDING: this header includes nothing from the engine and
 * nothing from the STL beyond <stdint.h>/<stddef.h>, uses no heap, and never
 * throws — so the SAME file compiles unmodified on an embedded flight computer
 * (Teensy/Arduino toolchains). It is the shared wire contract for
 * hardware-in-the-loop links: keep it dependency-free.
 *
 * WIRE FORMAT (one frame):
 *   COBS( payload .. CRC16-hi CRC16-lo ) 0x00
 *
 * - CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF) over the raw payload,
 *   appended big-endian BEFORE COBS encoding.
 * - COBS (Consistent Overhead Byte Stuffing) removes all interior zero bytes,
 *   so the single 0x00 delimiter unambiguously terminates each frame. A
 *   receiver resynchronizes after corruption by scanning to the next 0x00.
 *
 * RECEIVE PATTERN (both sides):
 *   accumulate bytes; on each 0x00, call DecodeFrame() on the bytes since the
 *   previous delimiter (exclusive). DecodeFrame returns 0 for corrupt/short
 *   frames — drop and continue.
 * ============================================================================
 */

#include <stdint.h>
#include <stddef.h>

namespace Cosmic
{
namespace Framing
{
    // =========================================================================
    // CRC16-CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no xorout)
    // Check value: Crc16Ccitt("123456789", 9) == 0x29B1
    // =========================================================================
    inline uint16_t Crc16Ccitt(const uint8_t* data, size_t length, uint16_t crc = 0xFFFF)
    {
        for (size_t i = 0; i < length; ++i)
        {
            crc = (uint16_t)(crc ^ ((uint16_t)data[i] << 8));
            for (int b = 0; b < 8; ++b)
                crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021)
                                     : (uint16_t)(crc << 1);
        }
        return crc;
    }

    // =========================================================================
    // Incremental COBS encoder — lets EncodeFrame append the CRC without
    // materializing a payload+CRC scratch buffer (no heap, embedded-friendly).
    // =========================================================================
    struct CobsWriter
    {
        uint8_t* Out;
        size_t   Cap;
        size_t   WriteIdx = 1;   // slot 0 is the first code byte
        size_t   CodeIdx  = 0;
        uint8_t  Code     = 1;
        bool     Overflow = false;

        CobsWriter(uint8_t* out, size_t cap) : Out(out), Cap(cap)
        {
            if (cap < 1) Overflow = true;
        }

        void Put(uint8_t b)
        {
            if (Overflow) return;

            if (b == 0)
            {
                Out[CodeIdx] = Code;
                Code    = 1;
                CodeIdx = WriteIdx++;
                if (WriteIdx > Cap) Overflow = true;
            }
            else
            {
                if (WriteIdx >= Cap) { Overflow = true; return; }
                Out[WriteIdx++] = b;
                ++Code;
                if (Code == 0xFF)   // group full — start a new one
                {
                    Out[CodeIdx] = Code;
                    Code    = 1;
                    CodeIdx = WriteIdx++;
                    if (WriteIdx > Cap) Overflow = true;
                }
            }
        }

        /** @return Encoded size, or 0 if the output buffer was too small. */
        size_t Finish()
        {
            if (Overflow) return 0;
            Out[CodeIdx] = Code;
            return WriteIdx;
        }
    };

    /** @brief One-shot COBS encode. Output capacity must be >= CobsMaxEncoded(length). */
    inline size_t CobsEncode(const uint8_t* input, size_t length, uint8_t* output, size_t outputCap)
    {
        CobsWriter w(output, outputCap);
        for (size_t i = 0; i < length; ++i)
            w.Put(input[i]);
        return w.Finish();
    }

    /**
     * @brief COBS decode. `input` must NOT contain the 0x00 delimiter.
     * @param outputCap must be >= length (decoded output is never longer than input).
     * @return Decoded size, or 0 on malformed input (embedded zero, truncated group,
     *         or insufficient output capacity). Note an empty payload also returns 0 —
     *         frames in this codec always carry >= 2 CRC bytes, so 0 is unambiguous.
     */
    inline size_t CobsDecode(const uint8_t* input, size_t length, uint8_t* output, size_t outputCap)
    {
        if (outputCap < length) return 0;

        size_t readIdx = 0, writeIdx = 0;
        while (readIdx < length)
        {
            const uint8_t code = input[readIdx];
            if (code == 0)                    return 0;  // delimiter inside a frame — malformed
            if (readIdx + code > length)      return 0;  // truncated group
            ++readIdx;

            for (uint8_t i = 1; i < code; ++i)
                output[writeIdx++] = input[readIdx++];

            if (code != 0xFF && readIdx < length)
                output[writeIdx++] = 0;
        }
        return writeIdx;
    }

    /** @brief Worst-case COBS-encoded size for n input bytes (one code byte per 254). */
    constexpr size_t CobsMaxEncoded(size_t n)
    {
        return n + n / 254 + 2;
    }

    /** @brief Worst-case full frame size (payload + CRC, COBS overhead, 0x00 delimiter). */
    constexpr size_t MaxFrameSize(size_t payloadLength)
    {
        return CobsMaxEncoded(payloadLength + 2) + 1;
    }

    // =========================================================================
    // Frame encode / decode
    // =========================================================================

    /**
     * @brief Encode payload -> COBS(payload + CRC16be) + 0x00 delimiter.
     * @param outCap must be >= MaxFrameSize(length).
     * @return Total bytes written (including the trailing 0x00), or 0 if out was too small.
     */
    inline size_t EncodeFrame(const uint8_t* payload, size_t length, uint8_t* out, size_t outCap)
    {
        if (outCap < 2) return 0;
        const uint16_t crc = Crc16Ccitt(payload, length);

        CobsWriter w(out, outCap - 1);            // reserve the delimiter byte
        for (size_t i = 0; i < length; ++i)
            w.Put(payload[i]);
        w.Put((uint8_t)(crc >> 8));
        w.Put((uint8_t)(crc & 0xFF));

        const size_t n = w.Finish();
        if (n == 0) return 0;

        out[n] = 0x00;
        return n + 1;
    }

    /**
     * @brief Decode one frame's bytes (the span BETWEEN delimiters — no 0x00 included).
     * @param outCap must be >= length.
     * @return Payload size on success; 0 on COBS corruption, short frame, or CRC mismatch.
     */
    inline size_t DecodeFrame(const uint8_t* frame, size_t length, uint8_t* out, size_t outCap)
    {
        const size_t n = CobsDecode(frame, length, out, outCap);
        if (n < 2) return 0;

        const uint16_t rxCrc = (uint16_t)(((uint16_t)out[n - 2] << 8) | out[n - 1]);
        if (Crc16Ccitt(out, n - 2) != rxCrc) return 0;

        return n - 2;
    }

} // namespace Framing
} // namespace Cosmic
