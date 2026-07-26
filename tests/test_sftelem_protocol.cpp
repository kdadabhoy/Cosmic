// test_sftelem_protocol.cpp — W9 (plan doc 28 §9.6).
//
// The SF_Telem wire protocol, tested directly. Telemetry.h is header-only and
// engine-free (it includes only <string> <vector> <cstdint> <cstdio> <cmath>),
// so it lifts straight into a test TU the same way test_template_scripts.cpp
// pulls in the Starforge template scripts.
//
// Why this matters: the bytes arriving on this path come off a Bluetooth link
// from a robot. Dropped, interleaved and half-written frames are the normal
// case, not the exceptional one — ParseFrame's whole job is to reject them
// without ever misreporting a value. It returns false on every error precisely
// so '#' heartbeat lines are ignored for free.
//
// Wire format (Telemetry.h:16-30):
//   $<S>,<temp>,<vraw>,<iraw>,<craw>,<erpmraw>*<HH>\n
//   checksum = XOR of every char between '$' and '*', two hex digits.

#include <doctest.h>

#include "../Projects/SF_Telem/src/Telemetry.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace Workspace;

namespace
{
    // Build a well-formed frame with a correct checksum. Everything in this file
    // starts from here and then breaks exactly one thing.
    std::string MakeFrame(char side, int temp, int vraw, int iraw, int craw, int erpm)
    {
        char payload[96];
        std::snprintf(payload, sizeof(payload), "%c,%d,%d,%d,%d,%d",
                      side, temp, vraw, iraw, craw, erpm);

        const uint8_t crc = Checksum(payload, payload + std::strlen(payload));

        char frame[128];
        std::snprintf(frame, sizeof(frame), "$%s*%02X", payload, crc);
        return frame;
    }
}

// =============================================================================
// ParseFrame — the happy path
// =============================================================================

TEST_CASE("ParseFrame decodes a well-formed frame for each side tag")
{
    struct Case { char tag; int expectedId; };
    const Case cases[] = { { 'R', ESC_RIGHT }, { 'L', ESC_LEFT }, { 'W', ESC_WEAPON } };

    for (const auto& c : cases)
    {
        RawPacket pkt;
        const std::string frame = MakeFrame(c.tag, 25, 1680, 420, 120, 350);
        CAPTURE(frame);

        REQUIRE(ParseFrame(frame, pkt));
        CHECK(pkt.id          == c.expectedId);
        CHECK(pkt.temp        == 25);
        CHECK(pkt.voltageRaw  == 1680);
        CHECK(pkt.currentRaw  == 420);
        CHECK(pkt.consumption == 120);
        CHECK(pkt.erpmRaw     == 350);
    }
}

TEST_CASE("ParseFrame accepts lower-case checksum digits")
{
    // %2x is case-insensitive, and firmware in the field has emitted both.
    std::string frame = MakeFrame('R', 25, 1680, 420, 120, 350);
    for (size_t i = frame.size() - 2; i < frame.size(); ++i)
        frame[i] = static_cast<char>(std::tolower(static_cast<unsigned char>(frame[i])));

    RawPacket pkt;
    CAPTURE(frame);
    CHECK(ParseFrame(frame, pkt));
}

TEST_CASE("ParseFrame accepts the boundary values the raw fields can carry")
{
    RawPacket pkt;

    REQUIRE(ParseFrame(MakeFrame('R', 0, 0, 0, 0, 0), pkt));
    CHECK(pkt.temp == 0);
    CHECK(pkt.voltageRaw == 0);
    CHECK(pkt.erpmRaw == 0);

    REQUIRE(ParseFrame(MakeFrame('W', 255, 65535, 65535, 65535, 65535), pkt));
    CHECK(pkt.temp == 255);
    CHECK(pkt.voltageRaw  == 65535);
    CHECK(pkt.currentRaw  == 65535);
    CHECK(pkt.consumption == 65535);
    CHECK(pkt.erpmRaw     == 65535);
}

// =============================================================================
// ParseFrame — rejection
// =============================================================================

TEST_CASE("ParseFrame rejects a checksum mismatch")
{
    std::string frame = MakeFrame('R', 25, 1680, 420, 120, 350);

    // Flip the low checksum nibble to something that cannot match.
    char& last = frame.back();
    last = (last == '0') ? '1' : '0';

    RawPacket pkt;
    pkt.id = 42;
    CAPTURE(frame);
    CHECK_FALSE(ParseFrame(frame, pkt));

    // A rejected frame must leave the caller's packet untouched — a half-written
    // `out` would silently poison the dashboard.
    CHECK(pkt.id == 42);
}

TEST_CASE("ParseFrame rejects a payload edited without recomputing the checksum")
{
    // The realistic corruption: a byte flipped in flight. Same length, same
    // shape, wrong content — the checksum is the only thing standing in the way.
    const std::string good = MakeFrame('R', 25, 1680, 420, 120, 350);

    for (size_t i = 1; i + 3 < good.size(); ++i)
    {
        std::string bad = good;
        if (bad[i] == '*' || bad[i] == ',') continue;
        bad[i] = (bad[i] == '9') ? '8' : '9';

        RawPacket pkt;
        CAPTURE(bad);
        CHECK_FALSE(ParseFrame(bad, pkt));
    }
}

TEST_CASE("ParseFrame ignores heartbeat and non-frame lines")
{
    RawPacket pkt;

    // '#' heartbeat lines are the documented reason ParseFrame returns false
    // rather than throwing or logging.
    CHECK_FALSE(ParseFrame("#alive", pkt));
    CHECK_FALSE(ParseFrame("# SF_Telem booting, 3 ESCs configured", pkt));
    CHECK_FALSE(ParseFrame("", pkt));
    CHECK_FALSE(ParseFrame("\n", pkt));
    CHECK_FALSE(ParseFrame("garbage", pkt));
    CHECK_FALSE(ParseFrame("R,25,1680,420,120,350*7F", pkt));   // no leading '$'
    CHECK_FALSE(ParseFrame("     ", pkt));
}

TEST_CASE("ParseFrame rejects an unknown side tag")
{
    // Only R / L / W route anywhere. Anything else must fail even with a
    // perfectly valid checksum.
    for (char tag : { 'X', 'Z', 'r', 'l', 'w', '1', '$', ' ' })
    {
        RawPacket pkt;
        const std::string frame = MakeFrame(tag, 25, 1680, 420, 120, 350);
        CAPTURE(frame);
        CHECK_FALSE(ParseFrame(frame, pkt));
    }
}

TEST_CASE("ParseFrame rejects a missing star")
{
    RawPacket pkt;
    CHECK_FALSE(ParseFrame("$R,25,1680,420,120,350", pkt));
    CHECK_FALSE(ParseFrame("$R,25,1680,420,120,3507F", pkt));

    // Star present but with too few checksum digits after it.
    CHECK_FALSE(ParseFrame("$R,25,1680,420,120,350*", pkt));
    CHECK_FALSE(ParseFrame("$R,25,1680,420,120,350*7", pkt));
}

TEST_CASE("ParseFrame rejects every truncation of a valid frame")
{
    // Exactly what a dropped Bluetooth packet looks like: a valid prefix.
    const std::string good = MakeFrame('L', 30, 1650, 900, 240, 700);

    for (size_t cut = 0; cut < good.size(); ++cut)
    {
        RawPacket pkt;
        const std::string partial = good.substr(0, cut);
        CAPTURE(partial);
        CHECK_FALSE(ParseFrame(partial, pkt));
    }

    // The whole thing still parses — the sweep above must not be passing by
    // rejecting everything.
    RawPacket pkt;
    CHECK(ParseFrame(good, pkt));
}

TEST_CASE("ParseFrame rejects non-numeric and short field lists")
{
    RawPacket pkt;

    // Each of these has a VALID checksum for its own payload, so only the field
    // scan can reject them. Built through MakeFrame's payload rules by hand.
    auto framed = [](const std::string& payload)
    {
        const uint8_t crc = Checksum(payload.data(), payload.data() + payload.size());
        char buf[160];
        std::snprintf(buf, sizeof(buf), "$%s*%02X", payload.c_str(), crc);
        return std::string(buf);
    };

    CHECK_FALSE(ParseFrame(framed("R,aa,bb,cc,dd,ee"), pkt));       // non-numeric
    CHECK_FALSE(ParseFrame(framed("R,25,1680"), pkt));              // too few fields
    CHECK_FALSE(ParseFrame(framed("R"), pkt));                      // tag only
    CHECK_FALSE(ParseFrame(framed("R,,,,,"), pkt));                 // empty fields
    CHECK_FALSE(ParseFrame(framed("R;25;1680;420;120;350"), pkt));  // wrong separator

    // Extra trailing fields are tolerated by sscanf (it stops after six), so this
    // one parses — pinned deliberately so a future tightening is a visible change.
    CHECK(ParseFrame(framed("R,25,1680,420,120,350,999"), pkt));
    CHECK(pkt.erpmRaw == 350);
}

TEST_CASE("ParseFrame never reads past the end of a hostile string")
{
    RawPacket pkt;

    // Stars and dollars in unhelpful places — the star index arithmetic
    // (star + 3 > size) is the guard being exercised here.
    CHECK_FALSE(ParseFrame("$*", pkt));
    CHECK_FALSE(ParseFrame("$****", pkt));
    CHECK_FALSE(ParseFrame("$$$$$$", pkt));
    CHECK_FALSE(ParseFrame(std::string("$R,25*") + std::string(3, '\0'), pkt));
    CHECK_NOTHROW((void)ParseFrame(std::string(4096, '$'), pkt));
    CHECK_NOTHROW((void)ParseFrame(std::string(4096, '*'), pkt));

    // Embedded NUL mid-frame: std::string keeps its length, but the sscanf calls
    // work on c_str() and stop at the NUL.
    std::string withNul = MakeFrame('R', 25, 1680, 420, 120, 350);
    withNul[3] = '\0';
    CHECK_NOTHROW((void)ParseFrame(withNul, pkt));
}

// =============================================================================
// Sample round-trips — the DataRecorder / DataPlayer channel contract
// =============================================================================

TEST_CASE("DriveSample survives ToChannels -> FromChannels exactly")
{
    DriveSample in;
    in.tempC       = 31.5f;
    in.voltageV    = 16.80f;
    in.currentA    = 4.20f;
    in.consumption = 1234.0f;
    in.eRPM        = 35000.0f;
    in.motorRPM    = 5833.25f;
    in.speedMph    = 12.75f;
    in.powerW      = 70.56f;

    const std::vector<float> ch = in.ToChannels();
    REQUIRE(ch.size() == DCH_COUNT);

    // Channel ORDER is the contract with the recorder — assert the slots, not
    // just the round-trip, or a transposition would round-trip happily.
    CHECK(ch[DCH_TEMP]   == doctest::Approx(in.tempC));
    CHECK(ch[DCH_VOLT]   == doctest::Approx(in.voltageV));
    CHECK(ch[DCH_CURR]   == doctest::Approx(in.currentA));
    CHECK(ch[DCH_CONS]   == doctest::Approx(in.consumption));
    CHECK(ch[DCH_ERPM]   == doctest::Approx(in.eRPM));
    CHECK(ch[DCH_MOTRPM] == doctest::Approx(in.motorRPM));
    CHECK(ch[DCH_SPEED]  == doctest::Approx(in.speedMph));
    CHECK(ch[DCH_POWER]  == doctest::Approx(in.powerW));

    const DriveSample out = DriveSample::FromChannels(ch);
    CHECK(out.tempC       == in.tempC);
    CHECK(out.voltageV    == in.voltageV);
    CHECK(out.currentA    == in.currentA);
    CHECK(out.consumption == in.consumption);
    CHECK(out.eRPM        == in.eRPM);
    CHECK(out.motorRPM    == in.motorRPM);
    CHECK(out.speedMph    == in.speedMph);
    CHECK(out.powerW      == in.powerW);
}

TEST_CASE("WeaponSample survives ToChannels -> FromChannels exactly")
{
    WeaponSample in;
    in.tempC       = 28.0f;
    in.voltageV    = 16.55f;
    in.currentA    = 9.00f;
    in.consumption = 880.0f;
    in.eRPM        = 70000.0f;
    in.motorRPM    = 23333.5f;
    in.weaponRPM   = 5833.375f;
    in.tipSpeedMph = 136.25f;
    in.powerW      = 148.95f;

    const std::vector<float> ch = in.ToChannels();
    REQUIRE(ch.size() == WCH_COUNT);

    CHECK(ch[WCH_TEMP]   == doctest::Approx(in.tempC));
    CHECK(ch[WCH_WPNRPM] == doctest::Approx(in.weaponRPM));
    CHECK(ch[WCH_TIP]    == doctest::Approx(in.tipSpeedMph));
    CHECK(ch[WCH_POWER]  == doctest::Approx(in.powerW));

    const WeaponSample out = WeaponSample::FromChannels(ch);
    CHECK(out.tempC       == in.tempC);
    CHECK(out.voltageV    == in.voltageV);
    CHECK(out.currentA    == in.currentA);
    CHECK(out.consumption == in.consumption);
    CHECK(out.eRPM        == in.eRPM);
    CHECK(out.motorRPM    == in.motorRPM);
    CHECK(out.weaponRPM   == in.weaponRPM);
    CHECK(out.tipSpeedMph == in.tipSpeedMph);
    CHECK(out.powerW      == in.powerW);
}

TEST_CASE("FromChannels tolerates short and empty frames")
{
    // Documented contract: replay frames from an older/partial recording must
    // not read out of bounds — missing channels read as zero.
    const DriveSample emptyDrive = DriveSample::FromChannels({});
    CHECK(emptyDrive.tempC    == 0.0f);
    CHECK(emptyDrive.powerW   == 0.0f);
    CHECK(emptyDrive.speedMph == 0.0f);

    const DriveSample shortDrive = DriveSample::FromChannels({ 20.0f, 16.0f });
    CHECK(shortDrive.tempC    == 20.0f);
    CHECK(shortDrive.voltageV == 16.0f);
    CHECK(shortDrive.currentA == 0.0f);
    CHECK(shortDrive.powerW   == 0.0f);

    const WeaponSample emptyWeapon = WeaponSample::FromChannels({});
    CHECK(emptyWeapon.weaponRPM   == 0.0f);
    CHECK(emptyWeapon.tipSpeedMph == 0.0f);

    // A drive-length frame fed to the weapon decoder (the mismatch that happens
    // when entity tags get crossed) must clamp, not fault.
    const WeaponSample crossed = WeaponSample::FromChannels(std::vector<float>(DCH_COUNT, 1.0f));
    CHECK(crossed.powerW == 0.0f);   // index 8 is past a drive frame's 8 channels
    CHECK(crossed.tempC  == 1.0f);
}

// =============================================================================
// Decode — the zero-guards
// =============================================================================

TEST_CASE("DriveSample::Decode guards against zero poles, gear ratio and slip")
{
    RawPacket pkt;
    pkt.id = ESC_RIGHT;
    pkt.temp = 25; pkt.voltageRaw = 1680; pkt.currentRaw = 420;
    pkt.consumption = 100; pkt.erpmRaw = 350;

    DriveConfig cfg;
    cfg.Poles = 0; cfg.GearRatio = 0.0f; cfg.SlipFactor = 0.0f;

    // Every one of these would be a division by zero without the guards; the
    // result must be finite, not inf/NaN on the dashboard.
    const DriveSample s = DriveSample::Decode(pkt, cfg);
    CHECK(std::isfinite(s.motorRPM));
    CHECK(std::isfinite(s.speedMph));
    CHECK(std::isfinite(s.powerW));
    CHECK(s.eRPM == doctest::Approx(35000.0f));
    CHECK(s.motorRPM == doctest::Approx(35000.0f));   // poles clamped to 2 => /1

    cfg.Poles = 1;   // still <= 1, still clamped to 2
    CHECK(std::isfinite(DriveSample::Decode(pkt, cfg).motorRPM));

    // Negative slip is not clamped (only == 0 is), but must stay finite.
    cfg.Poles = 12; cfg.GearRatio = 19.0f; cfg.SlipFactor = -1.0f;
    CHECK(std::isfinite(DriveSample::Decode(pkt, cfg).speedMph));
}

TEST_CASE("WeaponSample::Decode guards against zero poles and gear ratio")
{
    RawPacket pkt;
    pkt.id = ESC_WEAPON;
    pkt.temp = 28; pkt.voltageRaw = 1655; pkt.currentRaw = 900;
    pkt.consumption = 880; pkt.erpmRaw = 700;

    WeaponConfig cfg;
    cfg.Poles = 0; cfg.GearRatio = 0.0f;

    const WeaponSample s = WeaponSample::Decode(pkt, cfg);
    CHECK(std::isfinite(s.motorRPM));
    CHECK(std::isfinite(s.weaponRPM));
    CHECK(std::isfinite(s.tipSpeedMph));
    CHECK(std::isfinite(s.powerW));

    // Sane config: 6 poles, 4:1 — the numbers the real robot runs.
    cfg.Poles = 6; cfg.GearRatio = 4.0f;
    const WeaponSample real = WeaponSample::Decode(pkt, cfg);
    CHECK(real.eRPM      == doctest::Approx(70000.0f));
    CHECK(real.motorRPM  == doctest::Approx(70000.0f / 3.0f));
    CHECK(real.weaponRPM == doctest::Approx(70000.0f / 3.0f / 4.0f));
    CHECK(real.powerW    == doctest::Approx(16.55f * 9.0f).epsilon(0.001));
}

TEST_CASE("Id helpers round-trip and reject unknown tags")
{
    for (int id : { ESC_RIGHT, ESC_LEFT, ESC_WEAPON })
    {
        CHECK(IdFromChar(IdToChar(id)) == id);
        CHECK(std::string(IdLabel(id)).size() > 0);
        CHECK(IdEntity(id).rfind("ESC_", 0) == 0);
    }

    CHECK(IdFromChar('X') == -1);
    CHECK(IdFromChar('\0') == -1);
    CHECK(IsDrive(ESC_RIGHT));
    CHECK(IsDrive(ESC_LEFT));
    CHECK_FALSE(IsDrive(ESC_WEAPON));

    // Entity names are the DataRecorder keys — they must stay distinct.
    CHECK(IdEntity(ESC_RIGHT) != IdEntity(ESC_LEFT));
    CHECK(IdEntity(ESC_LEFT)  != IdEntity(ESC_WEAPON));

    CHECK(DriveChannelNames().size()  == DCH_COUNT);
    CHECK(WeaponChannelNames().size() == WCH_COUNT);
}

// =============================================================================
// Seeded mutation fuzz
// =============================================================================

TEST_CASE("ParseFrame survives seeded mutation fuzz")
{
    // Fixed seed so a failure reproduces exactly. Mutations are applied to real
    // frames, which is far more likely to reach the deep parsing paths than
    // random noise would.
    std::mt19937 rng(0x5F7E1EDu);
    std::uniform_int_distribution<int> byteDist(0, 255);
    std::uniform_int_distribution<int> countDist(1, 6);

    const std::string seeds[] = {
        MakeFrame('R', 25, 1680, 420, 120, 350),
        MakeFrame('L', 30, 1650, 900, 240, 700),
        MakeFrame('W', 28, 1655, 900, 880, 700),
        "#heartbeat",
        "$R,25,1680,420,120,350",
    };

    int parsed = 0;
    for (int iter = 0; iter < 400; ++iter)
    {
        std::string s = seeds[iter % (sizeof(seeds) / sizeof(seeds[0]))];
        if (s.empty()) continue;

        const int mutations = countDist(rng);
        for (int m = 0; m < mutations; ++m)
        {
            std::uniform_int_distribution<size_t> posDist(0, s.size() - 1);
            s[posDist(rng)] = static_cast<char>(byteDist(rng));
        }

        RawPacket pkt;
        CAPTURE(iter);
        bool ok = false;
        CHECK_NOTHROW(ok = ParseFrame(s, pkt));

        // Whenever it DOES accept a mutated frame, the result must still be
        // internally consistent — a valid id is the invariant the router relies
        // on (TelemHub indexes m_Drive[pkt.id] with it).
        if (ok)
        {
            ++parsed;
            CHECK(pkt.id >= 0);
            CHECK(pkt.id < ESC_COUNT);
        }
    }

    // Mutations land in the checksum far more often than not, so the vast
    // majority must be rejected. This is a smoke check on the fuzz itself: if
    // everything parsed, the mutations were not reaching the payload.
    CHECK(parsed < 200);
}
