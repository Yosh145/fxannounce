// Unit tests for Airline Packs: file-name parsing, legacy names, selection (Fenix tag rules), airline table and pack
// resolution (VP_SPEC.md § 1–3). The last cases scan the sample packs in ../Packs when they exist (never committed).
#include "core/airlines.h"
#include "core/pack_library.h"
#include "core/pack_resolver.h"
#include "core/selector.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>

using namespace fxa;

namespace {

Pack makePack(const std::string& name, const std::vector<std::string>& fileNames)
{
    Pack p;
    p.name = name;
    p.root = "/packs/" + name;
    for (const std::string& n : fileNames) {
        auto f = parseSoundFileName(n);
        REQUIRE(f);
        f->path = p.root + "/" + n;
        f->relPath = n;
        for (int v : f->variants) p.maxVariant = std::max(p.maxVariant, v);
        p.files.push_back(*f);
    }
    return p;
}

SelectContext ctxFor(Family fam, bool neo, TimeOfDay t)
{
    SelectContext c;
    c.aircraft = makeAircraftType(fam, neo);
    c.time = t;
    return c;
}

std::string pick(const Pack& p, AnnType t, const SelectContext& c, unsigned seed = 1)
{
    Rng rng(seed);
    const Selection s = selectSound(p, t, c, rng);
    return s.file ? s.file->relPath : std::string("(none)");
}

std::string packsDir() { return std::string(FXA_SOURCE_DIR) + "/../Packs"; }

} // namespace

TEST_CASE("file names: type, tags, display name")
{
    auto f = parseSoundFileName("BoardingWelcome[A321][Morning][1].ogg");
    REQUIRE(f);
    CHECK(f->type == AnnType::BoardingWelcome);
    CHECK(f->aircraft == std::vector<std::string>{ "A321" });
    CHECK(f->times.size() == 1);
    CHECK(f->variants == std::vector<int>{ 1 });
    CHECK(f->displayName == "BoardingWelcome[A321][Morning][1]");

    f = parseSoundFileName("BoardingWelcome[Prerecorded]~Welcome passengers.ogg");
    REQUIRE(f);
    CHECK(f->mode == Mode::Prerecorded);
    CHECK(f->displayName == "Welcome passengers");

    // Tags after the tilde belong to the display name (Fenix: "incorrect" form).
    f = parseSoundFileName("BoardingWelcome~Welcome passengers[Prerecorded].ogg");
    REQUIRE(f);
    CHECK(!f->mode);
    CHECK(f->displayName == "Welcome passengers[Prerecorded]");

    f = parseSoundFileName("BoardingWelcome[A319][USB][Refuelling][Night].ogg");
    REQUIRE(f);
    CHECK(f->refuel);
    CHECK(f->custom == std::vector<std::string>{ "USB" });

    f = parseSoundFileName("SafetyBriefing[Overwater][USB][WiFi]");
    REQUIRE(f);
    CHECK(f->overwater);
    CHECK(f->custom.size() == 2);

    f = parseSoundFileName("SafetyBriefing[A319][A320][Night].ogg");
    REQUIRE(f);
    CHECK(f->aircraft.size() == 2);

    f = parseSoundFileName("DescentStarted[Descent][Approach].ogg");
    REQUIRE(f);
    CHECK(f->phases.size() == 2);

    f = parseSoundFileName("MyCustomAnnouncement~Duty free.ogg");
    REQUIRE(f);
    CHECK(!f->type);

    CHECK(!parseSoundFileName("[A320].ogg"));
    f = parseSoundFileName("ArmDoors[Morning.ogg");
    REQUIRE(f);
    CHECK(!f->warnings.empty());
}

TEST_CASE("legacy names map to Fenix types with a call context")
{
    auto f = parseSoundFileName("CrewSeatsLanding[1].ogg");
    REQUIRE(f);
    CHECK(f->type == AnnType::CrewTakeSeats);
    CHECK(f->context == CallContext::Landing);
    CHECK(f->legacy);
    CHECK(parseSoundFileName("CallCabinSecureTakeoff.ogg")->type == AnnType::CabinSecured);
    CHECK(parseSoundFileName("CallCabinSecureTakeoff.ogg")->effectiveMode() == Mode::Interphone);
    CHECK(parseSoundFileName("DescentSeatbelt.ogg")->type == AnnType::DescentStarted);   // EJU's spelling
    CHECK(parseSoundFileName("DescentSeatbelts.ogg")->type == AnnType::DescentStarted);
    CHECK(parseSoundFileName("FastenSeatbelt.ogg")->type == AnnType::SeatbeltSignOn);
    CHECK(parseSoundFileName("DisembarkStarted.ogg")->type == AnnType::DeboardingStarted);
    CHECK(parseSoundFileName("SafetyBriefing.ogg")->effectiveMode() == Mode::Prerecorded);
    CHECK(parseSoundFileName("ArmDoors.ogg")->effectiveMode() == Mode::Manual);
}

TEST_CASE("time of day spans")
{
    CHECK(timeOfDayAt(0) == TimeOfDay::Night);
    CHECK(timeOfDayAt(5.99 * 3600) == TimeOfDay::Night);
    CHECK(timeOfDayAt(6 * 3600) == TimeOfDay::Morning);
    CHECK(timeOfDayAt(12 * 3600) == TimeOfDay::Afternoon);
    CHECK(timeOfDayAt(19 * 3600) == TimeOfDay::Evening);
    CHECK(timeOfDayAt(86400 + 3600) == TimeOfDay::Night);
}

TEST_CASE("selection: most matching tags wins, every category must hold")
{
    const Pack dlh = makePack("DLH", { "BoardingWelcome[Afternoon].ogg", "BoardingWelcome[Evening].ogg",
                                       "BoardingWelcome[Refueling][Evening].ogg", "BoardingWelcome[Refueling][Morning].ogg",
                                       "DescentSeatbelts.ogg", "DescentSeatbelts[Evening].ogg" });
    SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Evening);
    CHECK(pick(dlh, AnnType::BoardingWelcome, c) == "BoardingWelcome[Evening].ogg");
    c.refueling = true;
    CHECK(pick(dlh, AnnType::BoardingWelcome, c) == "BoardingWelcome[Refueling][Evening].ogg");
    c.time = TimeOfDay::Night;
    CHECK(pick(dlh, AnnType::BoardingWelcome, c) == "(none)");   // nothing for night, and no untagged file
    CHECK(pick(dlh, AnnType::DescentStarted, c) == "DescentSeatbelts.ogg");
    c.time = TimeOfDay::Evening;
    CHECK(pick(dlh, AnnType::DescentStarted, c) == "DescentSeatbelts[Evening].ogg");
}

TEST_CASE("selection: aircraft type tags are hierarchical")
{
    const Pack ual = makePack("UAL", { "SafetyBriefing[A21N].ogg", "SafetyBriefing[A319].ogg", "SafetyBriefing[A320].ogg",
                                       "SafetyBriefing[B738].ogg", "PreSafetyBriefing.ogg", "ArmDoors[A321].ogg",
                                       "ArmDoors[A21N].ogg" });
    CHECK(pick(ual, AnnType::SafetyBriefing, ctxFor(Family::A321, true, TimeOfDay::Morning)) == "SafetyBriefing[A21N].ogg");
    CHECK(pick(ual, AnnType::SafetyBriefing, ctxFor(Family::A321, false, TimeOfDay::Morning)) == "(none)");
    CHECK(pick(ual, AnnType::SafetyBriefing, ctxFor(Family::A330, false, TimeOfDay::Morning)) == "(none)");
    CHECK(pick(ual, AnnType::SafetyBriefing, ctxFor(Family::A320, true, TimeOfDay::Morning)) == "SafetyBriefing[A320].ogg");
    CHECK(pick(ual, AnnType::SafetyBriefing, ctxFor(Family::A319, false, TimeOfDay::Morning)) == "SafetyBriefing[A319].ogg");
    // The specific tag beats the family tag on a neo; the family tag still plays on the ceo.
    for (unsigned seed = 1; seed < 20; ++seed)
        CHECK(pick(ual, AnnType::ArmDoors, ctxFor(Family::A321, true, TimeOfDay::Morning), seed) == "ArmDoors[A21N].ogg");
    CHECK(pick(ual, AnnType::ArmDoors, ctxFor(Family::A321, false, TimeOfDay::Morning)) == "ArmDoors[A321].ogg");
}

TEST_CASE("selection: legacy context, custom tags, overwater, phases")
{
    const Pack p = makePack("XXX", { "CrewSeatsTakeoff.ogg", "CrewSeatsLanding.ogg", "CrewTakeSeats[Approach].ogg",
                                     "SafetyBriefing.ogg", "SafetyBriefing[Overwater].ogg", "SafetyBriefing[WiFi][USB].ogg" });
    SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Morning);
    c.context = CallContext::Takeoff;
    CHECK(pick(p, AnnType::CrewTakeSeats, c) == "CrewSeatsTakeoff.ogg");
    c.context = CallContext::Landing;
    c.phase = Phase::Descent;
    CHECK(pick(p, AnnType::CrewTakeSeats, c) == "CrewSeatsLanding.ogg");
    c.phase = Phase::Approach;
    // Both score 1 (context bonus vs [Approach]); either is a valid Fenix choice.
    const std::string landing = pick(p, AnnType::CrewTakeSeats, c);
    CHECK((landing == "CrewSeatsLanding.ogg" || landing == "CrewTakeSeats[Approach].ogg"));

    CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing.ogg");
    c.overwater = true;
    CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing[Overwater].ogg");
    c.overwater = false;
    c.customTags = { "WiFi" };   // USB missing: the [WiFi][USB] file is not eligible
    CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing.ogg");
    c.customTags = { "wifi", "USB" };
    CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing[WiFi][USB].ogg");
}

TEST_CASE("selection: variants and the variant 0 fallback")
{
    const Pack ual = makePack("UAL", { "BoardingComplete.ogg", "BoardingComplete[1].ogg", "AfterTakeoff[1].ogg",
                                       "ArmDoors.ogg" });
    CHECK(ual.maxVariant == 1);
    SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Morning);
    c.variant = 1;
    CHECK(pick(ual, AnnType::BoardingComplete, c) == "BoardingComplete[1].ogg");
    CHECK(pick(ual, AnnType::AfterTakeoff, c) == "AfterTakeoff[1].ogg");
    CHECK(pick(ual, AnnType::ArmDoors, c) == "ArmDoors.ogg");   // no variant-1 file: variant 0 fallback
    c.variant = 0;
    CHECK(pick(ual, AnnType::BoardingComplete, c) == "BoardingComplete.ogg");
    CHECK(pick(ual, AnnType::AfterTakeoff, c) == "(none)");     // only a variant-1 file exists
    Rng rng(7);
    for (int k = 0; k < 50; ++k) {
        const int v = pickVariant(ual, rng);
        CHECK((v == 0 || v == 1));
    }
}

TEST_CASE("selection: ties avoid the file played last")
{
    const Pack p = makePack("XXX", { "BoardingWelcome[1].ogg", "BoardingWelcome~a.ogg", "BoardingWelcome~b.ogg" });
    SelectContext c = ctxFor(Family::A321, false, TimeOfDay::Morning);
    Rng rng(3);
    std::string last;
    for (int k = 0; k < 30; ++k) {
        const Selection s = selectSound(p, AnnType::BoardingWelcome, c, rng, last);
        REQUIRE(s.file);
        CHECK(s.file->path != last);
        CHECK(s.file->relPath != "BoardingWelcome[1].ogg");   // variant 1 is not the session variant
        last = s.file->path;
        CHECK(s.candidates.size() == 3);
    }
}

TEST_CASE("airline table: registrations, names, IATA")
{
    CHECK(findRegistrations("[LEA] EasyJet (G-UZHH)") == std::vector<std::string>{ "G-UZHH" });
    CHECK(findRegistrations("Delta Air Lines N427DX 8K") == std::vector<std::string>{ "N427DX" });
    CHECK(findRegistrations("Lufthansa (D-AIJB) 8K") == std::vector<std::string>{ "D-AIJB" });
    CHECK(findRegistrations("Air Canada (CFM - fences - C-FGKN) 8K") == std::vector<std::string>{ "C-FGKN" });
    CHECK(normalizeWords("[LEA] EasyJet (G-UZHH)") == "lea easyjet g uzhh");

    const AirlineTable t = AirlineTable::builtin();
    CHECK(t.size() > 100);
    CHECK(t.icaoForIata("LH") == std::vector<std::string>{ "DLH" });
    CHECK(t.icaoForLiveryName("[LEA] EasyJet (G-UZHH)").front() == "EZY");
    CHECK(t.icaoForLiveryName("easyJet Europe OE-IJA").front() == "EJU");
    CHECK(t.icaoForLiveryName("easyJet HB-JXA").front() == "EZS");
    CHECK(t.icaoForLiveryName("British Airways (G-EUXG) crest livery 8K").front() == "BAW");
    CHECK(t.icaoForLiveryName("Air India VT-EXS (8K)").front() == "AIC");
    CHECK(t.icaoForLiveryName("Airbus House F-WTTE (Airbus Colors)").empty());

    AirlineTable user = AirlineTable::parse("XYZ\tLH\tLufthansa\n");
    AirlineTable all = AirlineTable::builtin();
    all.prepend(user);
    CHECK(all.icaoForIata("LH").front() == "XYZ");
}

TEST_CASE("pack resolution order (VP_SPEC § 1.4)")
{
    PackLibrary lib;
    for (const char* n : { "AAL", "BAW", "DAL", "DLH", "EZY", "JBU", "LCA", "Default" }) lib.packs.push_back(makePack(n, { "ArmDoors.ogg" }));
    Pack ales = makePack("UALES", { "ArmDoors.ogg" });
    ales.mappingOnly = true;
    lib.packs.push_back(ales);
    const AirlineTable t = AirlineTable::builtin();
    auto res = [&](const std::string& livery, const std::string& fn = "", const LiveryConfig& cfg = {},
                   const std::string& ofp = "") {
        const Resolution r = resolvePack(lib, t, ResolveInput{ livery, fn, cfg, ofp });
        return r.pack ? r.pack->name : std::string("(none)");
    };
    CHECK(res("British Airways (G-EUXG) crest livery 8K") == "BAW");
    CHECK(res("Lufthansa (D-AIJB) 8K") == "DLH");
    CHECK(res("[LEA] EasyJet (G-UZHH)") == "EZY");
    CHECK(res("Airbus House F-WTTE (Airbus Colors)") == "Default");
    CHECK(res("Delta Air Lines N427DX 8K") == "DAL");
    CHECK(res("JetBlue Balloons N957JB Knock Knock Blue's There") == "JBU");
    CHECK(res("(IAE) American 'US Airways Heritage' (N578UW)") == "AAL");   // AWE not installed: next name
    CHECK(res("Cathay Pacific B-LCA") == "Default");                          // LCA is the registration, not a code
    CHECK(res("DLH - Lufthansa D-AIUA") == "DLH");
    CHECK(res("") == "Default");

    // Flight number beats the livery name; IATA goes through the table.
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "DLH400") == "DLH");
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "LH400") == "DLH");
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "XYZ123") == "BAW");

    // SimBrief OFP callsign (checked by the app) beats the MCDU number and the livery name, not the mappings.
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "BAW12", {}, "DLH400") == "DLH");
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "", {}, "EZY12") == "EZY");
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "DLH400", {}, "XYZ1") == "DLH");   // not installed: next
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "", {}, "") == "BAW");             // failed check: ""
    {
        LiveryConfig m;
        m.airline = "JBU";
        m.source = "liveries.ini [x]";
        CHECK(res("British Airways (G-EUXG) crest livery 8K", "", m, "DLH400") == "JBU");
        const Resolution r = resolvePack(lib, t, ResolveInput{ "x", "", {}, "DLH400" });
        CHECK(r.reason == "SimBrief OFP DLH400");
    }

    // Livery mapping beats everything; mapping-only packs are reachable only this way.
    LiveryConfig cfg;
    cfg.airline = "UALES";
    cfg.source = "airlinepack.cfg";
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "DLH400", cfg) == "UALES");
    CHECK(res("Some UALES livery") == "Default");
    cfg.airline = "QQQ";   // not installed: falls through
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "", cfg) == "BAW");

    const Ini central = Ini::parse("[Airbus House*]\nairline = DLH\ncustom_tags = WiFi\n[*]\noverwater = yes\n");
    const LiveryConfig c1 = liveryConfigFromCentral(central, "Airbus House F-WTTE (Airbus Colors)");
    CHECK(c1.airline == "DLH");
    CHECK(c1.customTags == std::vector<std::string>{ "WiFi" });
    CHECK(c1.source == "liveries.ini [Airbus House*]");
    CHECK(res("Airbus House F-WTTE (Airbus Colors)", "", c1) == "DLH");
    const LiveryConfig c2 = liveryConfigFromCentral(central, "Lufthansa");
    CHECK(c2.overwater == true);

    const Ini side = Ini::parse("icao_airline = BAW\nfnx_airline_pack_custom_tags = USB, WiFi\noverwater = auto\n");
    const LiveryConfig s = liveryConfigFromSidecar(side);
    CHECK(s.airline == "BAW");
    CHECK(s.customTags.size() == 2);
    CHECK(!s.overwater);
    const LiveryConfig m = mergeLiveryConfig(s, c1);
    CHECK(m.airline == "BAW");
    CHECK(m.source == "airlinepack.cfg");
}

TEST_CASE("sample packs in ../Packs (local only)")
{
    std::error_code ec;
    if (!std::filesystem::is_directory(packsDir(), ec)) {
        MESSAGE("../Packs not present: skipped");
        return;
    }
    const PackLibrary lib = scanPacks({ packsDir() });
    REQUIRE(lib.find("UAL"));
    REQUIRE(lib.find("DLH"));
    REQUIRE(lib.find("EJU"));
    REQUIRE(lib.find("UALES"));
    CHECK(lib.find("UAL")->files.size() == 30);
    CHECK(lib.find("UALES")->files.size() == 4);
    CHECK(lib.find("UALES")->mappingOnly);
    CHECK(lib.find("EJU")->files.size() == 20);
    CHECK(lib.find("DLH")->files.size() == 33);
    CHECK(lib.find("DLH")->wrapper == "DLH OLD PA SYSTEM");
    CHECK(lib.soundCount() == 87);
    const auto& w = lib.find("UAL")->warnings;
    CHECK(std::any_of(w.begin(), w.end(), [](const std::string& s) { return s.find(".mp4") != std::string::npos; }));
    // Every sample file has a known type (all legacy or Fenix names).
    for (const Pack& p : lib.packs)
        for (const SoundFile& f : p.files) CHECK_MESSAGE(f.type.has_value(), p.name, "/", f.relPath);

    SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Evening);
    c.refueling = true;
    CHECK(pick(*lib.find("DLH"), AnnType::BoardingWelcome, c) == "BoardingWelcome[Refueling][Evening].ogg");
    c = ctxFor(Family::A321, false, TimeOfDay::Afternoon);
    CHECK(pick(*lib.find("EJU"), AnnType::AfterLanding, c) == "AfterLanding[Afternoon].ogg");
    CHECK(pick(*lib.find("EJU"), AnnType::DescentStarted, c) == "DescentSeatbelt.ogg");
    CHECK(pick(*lib.find("UAL"), AnnType::SafetyBriefing, ctxFor(Family::A321, true, TimeOfDay::Morning)) == "SafetyBriefing[A21N].ogg");
    CHECK(pick(*lib.find("UAL"), AnnType::SafetyBriefing, ctxFor(Family::A321, false, TimeOfDay::Morning)) == "(none)");
    c = ctxFor(Family::A320, false, TimeOfDay::Morning);
    c.variant = 1;
    CHECK(pick(*lib.find("UAL"), AnnType::BoardingComplete, c) == "BoardingComplete[1].ogg");
    c.context = CallContext::Landing;
    CHECK(pick(*lib.find("UAL"), AnnType::CrewTakeSeats, c) == "CrewSeatsLanding.ogg");   // [1] is the skipped .mp4
}
