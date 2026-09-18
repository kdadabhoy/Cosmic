#include <doctest.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include "../Projects/SF_Telem/src/Telemetry.h"

namespace ShippingKiss
{
    struct Stream
    {
        std::deque<uint8_t> bytes;
        std::string output;
        int available() { return static_cast<int>(bytes.size()); }
        int read() { if (bytes.empty()) return -1; int b = bytes.front(); bytes.pop_front(); return b; }
        void print(const char* text) { output += text; }
        bool connected() { return true; }
    };
    Stream Serial, SerialBT;
    #include "wo05_kiss.inc"
}

TEST_CASE("WO-05 T06 KISS: shipping CRC8 and resync, separately from PC text and COBS")
{
    using namespace ShippingKiss;
    uint8_t check[] = {'1','2','3','4','5','6','7','8','9'};
    CHECK(get_crc8(check, 9) == 0xF4); // independent CRC-8/SMBUS known vector
    uint8_t frame[] = {25, 6, 144, 1, 164, 0, 120, 1, 94, 0};
    // Independent polynomial division for the specimen's stored CRC.
    unsigned residue = 0;
    for (int i = 0; i < 9; ++i)
    {
        residue ^= static_cast<unsigned>(frame[i]) << 8;
        for (int bit = 0; bit < 8; ++bit)
            residue = (residue & 0x8000) ? (residue << 1) ^ 0x10700 : residue << 1;
        residue &= 0xFFFF;
    }
    frame[9] = static_cast<uint8_t>(residue >> 8);
    ESC_Data data;
    REQUIRE(parseESC(frame, data));
    CHECK(data.temp == 25); CHECK(data.voltage == 1680);
    CHECK(data.current == 420); CHECK(data.consumption == 120); CHECK(data.erpm == 350);
    frame[4] ^= 1;
    CHECK_FALSE(parseESC(frame, data));
    frame[4] ^= 1;
    for (int split = 0; split <= 10; ++split)
    {
        Serial.output.clear(); SerialBT.output.clear();
        Stream wire;
        FrameSync sync;
        wire.bytes = {0xAA, 0xBB, 0xCC};
        for (int i = 0; i < split; ++i) wire.bytes.push_back(frame[i]);
        serviceKiss(wire, 'R', sync);
        for (int i = split; i < 10; ++i) wire.bytes.push_back(frame[i]);
        serviceKiss(wire, 'R', sync);
        REQUIRE(sync.good == 1);
        CHECK(sync.drops == 3); CHECK(sync.len == 0); CHECK(sync.bytes == 13);
        Workspace::RawPacket parsed;
        REQUIRE(Workspace::ParseFrame(Serial.output.substr(0, Serial.output.size()-1), parsed));
        CHECK(parsed.voltageRaw == 1680); CHECK(parsed.currentRaw == 420);
        CHECK(Serial.output == SerialBT.output);
    }
}
