// Audio without X-Plane: the Ogg decoder on the sample packs (local only), the decode cache, the chime, and the
// hearing model (VP_SPEC.md § 4.5).
#include "audio/chime.h"
#include "audio/decode_cache.h"
#include "audio/decoder.h"
#include "audio/hearing.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace fxa;

namespace {
std::string packsDir() { return std::string(FXA_SOURCE_DIR) + "/../Packs"; }
bool havePacks()
{
    std::error_code ec;
    return std::filesystem::is_directory(packsDir(), ec);
}
} // namespace

TEST_CASE("decoder rejects what is not Ogg Vorbis")
{
    const std::string junk = std::string(FXA_SOURCE_DIR) + "/../build/fx_junk_test.ogg";
    {
        std::ofstream f(junk, std::ios::binary);
        f << "this is not an ogg file at all";
    }
    const DecodeResult r = decodeOgg(junk);
    CHECK(!r.pcm);
    CHECK(!r.error.empty());
    CHECK(!decodeOgg("/nonexistent/x.ogg").pcm);
    std::filesystem::remove(junk);
}

TEST_CASE("decoder on the sample packs (local only)")
{
    if (!havePacks()) return;
    const DecodeResult r = decodeOgg(packsDir() + "/EJU/BoardingWelcome.ogg");   // 48 kHz stereo, 34 s
    REQUIRE(r.pcm);
    CHECK(r.pcm->rate == 48000);
    CHECK(r.pcm->seconds() == doctest::Approx(33.97).epsilon(0.01));
    int16_t peak = 0;
    for (int16_t v : r.pcm->samples) peak = std::max<int16_t>(peak, static_cast<int16_t>(std::abs(v)));
    CHECK(peak > 1000);   // real audio, downmixed to mono
    // The stray video in the UAL sample is rejected.
    CHECK(!decodeOgg(packsDir() + "/UAL/CrewSeatsLanding[1].mp4").pcm);
    // A short limit rejects a long file before decoding all of it.
    DecodeLimits lim;
    lim.maxSeconds = 10;
    CHECK(!decodeOgg(packsDir() + "/EJU/BoardingWelcome.ogg", lim).pcm);
}

TEST_CASE("decode cache: background decode, failures remembered")
{
    if (!havePacks()) return;
    DecodeCache cache(64u << 20);
    const std::string ok = packsDir() + "/EJU/ArmDoors[Morning].ogg";
    const std::string bad = packsDir() + "/UAL/CrewSeatsLanding[1].mp4";
    CHECK(!cache.get(ok));
    CHECK(!cache.get(bad));
    std::shared_ptr<PcmBuffer> pcm;
    for (int k = 0; k < 200 && !pcm; ++k) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        pcm = cache.get(ok);
    }
    REQUIRE(pcm);
    for (int k = 0; k < 100 && !cache.failed(bad); ++k) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    std::string err;
    CHECK(cache.failed(bad, &err));
    CHECK(!err.empty());
    CHECK(cache.bytes() == pcm->bytes());
    cache.shutdown();
}

TEST_CASE("cabin chime")
{
    const auto c = makeCabinChime(22050);
    REQUIRE(c);
    CHECK(c->seconds() > 1.5);
    CHECK(c->seconds() < 3.0);
    int16_t peak = 0;
    for (int16_t v : c->samples) peak = std::max<int16_t>(peak, static_cast<int16_t>(std::abs(v)));
    CHECK(peak > 20000);
    CHECK(peak < 32767);
}

TEST_CASE("hearing model")
{
    Settings set;
    SimState s;
    set.hearingFloor = 0.2f;
    set.doorLeak = 0.6f;
    CHECK(cabinHearing(set, s) == doctest::Approx(0.2f));      // door closed, PA knob in: the floor
    s.cockpitDoor = 1;
    CHECK(cabinHearing(set, s) == doctest::Approx(0.6f));      // door open
    s.paOn = true;
    s.paVol = 0.9f;
    CHECK(cabinHearing(set, s) == doctest::Approx(0.9f));      // PA knob pulled
    s.cameraInCabin = true;
    CHECK(cabinHearing(set, s) == doctest::Approx(1.0f));
    s.viewExternal = true;
    set.exteriorGain = 0.1f;
    CHECK(cabinHearing(set, s) == doctest::Approx(0.1f));
    set.realisticHearing = false;
    CHECK(cabinHearing(set, s) == doctest::Approx(1.0f));
    CHECK(soundGain(SoundKind::Pa, set, s, true) == 0.0f);    // muted
    CHECK(soundGain(SoundKind::Preview, set, s, true) > 0.0f); // previews ignore mute
    s.cabOn = true;
    s.cabVol = 0.5f;
    set.masterVolume = 1;
    set.interphoneVolume = 1;
    CHECK(soundGain(SoundKind::Interphone, set, s, false) == doctest::Approx(0.5f));
}

TEST_CASE("hearing follows ToLiss's door blend and internal volume")
{
    Settings set;
    set.hearingFloor = 0.1f;
    set.doorLeak = 0.8f;
    SimState s;
    s.cockpitDoor = 0;          // the angle says closed...
    s.doorBlend = 0.5f;         // ...ToLiss's sound blend says half open
    CHECK(doorOpenFraction(set, s) == doctest::Approx(0.5f));
    CHECK(cabinHearing(set, s) == doctest::Approx(0.4f));
    set.useTolissDoorBlend = false;
    CHECK(doorOpenFraction(set, s) == doctest::Approx(0.0f));
    set.useTolissDoorBlend = true;
    s.doorBlend = -1;           // not available: the angle
    s.cockpitDoor = 1;
    CHECK(doorOpenFraction(set, s) == doctest::Approx(1.0f));

    set.masterVolume = 1;
    set.chimeVolume = 1;
    s.tolissVolInternal = 0.5f;
    s.tolissVolOverride = -1;   // switch unknown: not followed
    CHECK(soundGain(SoundKind::Chime, set, s, false) == doctest::Approx(1.0f));
    s.tolissVolOverride = 0;    // override off: X-Plane's sliders already apply
    CHECK(soundGain(SoundKind::Chime, set, s, false) == doctest::Approx(1.0f));
    s.tolissVolOverride = 1;
    CHECK(soundGain(SoundKind::Chime, set, s, false) == doctest::Approx(0.5f));
    CHECK(soundGain(SoundKind::Preview, set, s, false) == doctest::Approx(1.0f));   // previews ignore it
    set.followTolissVolume = false;
    CHECK(soundGain(SoundKind::Chime, set, s, false) == doctest::Approx(1.0f));
}
