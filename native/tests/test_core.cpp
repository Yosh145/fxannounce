// Unit tests for the pure core: strings, config, aircraft types.
#include "core/aircraft.h"
#include "core/config.h"
#include "core/strings.h"

#include <doctest/doctest.h>

using namespace fxa;

TEST_CASE("strings")
{
    CHECK(iequals("Refuelling", "REFUELLING"));
    CHECK(!iequals("A320", "A321"));
    CHECK(trim("  a b \t") == "a b");
    CHECK(splitList(" WiFi, USB ;;x") == std::vector<std::string>{ "WiFi", "USB", "x" });
    CHECK(globMatch("British Airways*", "british airways (G-EUXG) crest livery 8K"));
    CHECK(globMatch("*(G-*", "[LEA] EasyJet (G-UZHH)"));
    CHECK(!globMatch("Lufthansa*", "Air Lufthansa"));
    CHECK(globMatch("A?2?", "A321"));
    const char raw[8] = { 'D', 'L', 'H', '4', ' ', '\0', 'x', 'y' };
    CHECK(cleanCString(raw, sizeof(raw)) == "DLH4");
}

TEST_CASE("ini parse and serialize")
{
    const Ini ini = Ini::parse("\xEF\xBB\xBF# comment\nroot = 1\n[British Airways*]\nairline = BAW   # inline\n"
                               "custom_tags = \"WiFi,USB\"\n\n[ x ]\nk=v\n");
    REQUIRE(ini.get("", "root"));
    CHECK(*ini.get("", "root") == "1");
    CHECK(*ini.get("british airways*", "AIRLINE") == "BAW");
    CHECK(*ini.get("British Airways*", "custom_tags") == "WiFi,USB");
    CHECK(*ini.get("x", "k") == "v");
    CHECK(ini.get("x", "missing") == nullptr);
    const Ini again = Ini::parse(ini.serialize());
    CHECK(*again.get("British Airways*", "airline") == "BAW");
}

TEST_CASE("settings round trip and clamping")
{
    Settings s;
    s.masterVolume = 0.5f;
    s.satcomTags = "WiFi";
    s.interphoneRetries = 2;
    s.disabledEvents = { "CabinService" };
    Ini ini;
    storeSettings(s, ini);
    Settings t;
    loadSettings(Ini::parse(ini.serialize()), t);
    CHECK(t.masterVolume == doctest::Approx(0.5f));
    CHECK(t.satcomTags == "WiFi");
    CHECK(t.interphoneRetries == 2);
    CHECK(!t.eventEnabled("cabinservice"));
    CHECK(t.eventEnabled("AfterTakeoff"));

    Settings u;
    loadSettings(Ini::parse("[general]\nmaster_volume = 9\nlog_level = x\n[timing]\nwelcome_min_s = 500\nwelcome_max_s = 200\n"), u);
    CHECK(u.masterVolume == doctest::Approx(2.0f));   // clamped to the UI range
    CHECK(u.logLevel == 2);                           // bad value keeps the default
    CHECK(u.welcomeMin == doctest::Approx(200));      // min/max swapped back into order
    CHECK(u.welcomeMax == doctest::Approx(500));
}

TEST_CASE("aircraft families and type tags")
{
    CHECK(familyFrom("A321", "a321.acf") == Family::A321);
    CHECK(familyFrom("A20N", "a320.acf") == Family::A320);
    CHECK(familyFrom("A339", "A330-900.acf") == Family::A330);
    CHECK(familyFrom("", "a321_StdDef.acf") == Family::A321);
    CHECK(familyFrom("C172", "Cessna_172SP.acf") == Family::Unknown);

    CHECK(engineIsNeo("LEA") == 1);
    CHECK(engineIsNeo("PWG") == 1);
    CHECK(engineIsNeo("IAE") == 0);
    CHECK(engineIsNeo("") == -1);
    CHECK(liveryStringCode("[IAEST] Lufthansa") == "IAEST");
    CHECK(liveryStringCode("Lufthansa (D-AIJB) 8K").empty());

    const AircraftType n = makeAircraftType(Family::A321, true);
    CHECK(n.tags == std::vector<std::string>{ "A321", "A21N" });
    CHECK(n.specific == "A21N");
    CHECK(makeAircraftType(Family::A321, false).specific == "A321");
    CHECK(makeAircraftType(Family::A330, false).tags == std::vector<std::string>{ "A330", "A339" });

    for (const char* t : { "A320", "A21N", "A20N", "A319", "B738", "B38M", "B77W", "B788", "E190", "E75L", "CRJ9", "DH8D" })
        CHECK_MESSAGE(isAircraftTypeTag(t), t);
    for (const char* t : { "Morning", "1", "WiFi", "USB", "Refueling", "Overwater", "GA", "Done", "KDFW", "ARR" })
        CHECK_MESSAGE(!isAircraftTypeTag(t), t);
}

#include "core/recorder.h"

TEST_CASE("recorder trace round trip")
{
    SimState s;
    s.t = 12.5;
    s.onGround = false;
    s.gsKt = 250;
    s.aglFt = 9000;
    s.fmgsPhaseRaw = 2;
    s.fmgsPhase = Phase::Climb;
    s.seatbelt = true;
    s.doorMax = 0.5f;
    s.pax = 180;
    s.paOn = true;
    s.paVol = 0.75f;
    s.flightNumber = "BAW12,3";   // a comma forces quoting
    s.acfIcao = "A321";
    s.satcom = true;
    const std::string text = std::string(traceHeader()) + "\n" + traceLine(s) + "\nE,13.00" + std::string(41, ',') +
                             ",ArmDoors played\n";
    const std::vector<TraceRow> rows = readTrace(text);
    REQUIRE(rows.size() == 2);
    const SimState& r = rows[0].state;
    CHECK(!rows[0].isEvent);
    CHECK(r.t == doctest::Approx(12.5));
    CHECK(!r.onGround);
    CHECK(r.gsKt == doctest::Approx(250));
    CHECK(r.fmgsPhase == Phase::Climb);
    CHECK(r.seatbelt);
    CHECK(r.pax == 180);
    CHECK(r.paVol == doctest::Approx(0.75f));
    CHECK(r.flightNumber == "BAW12,3");
    CHECK(r.acfIcao == "A321");
    CHECK(r.satcom);
    CHECK(rows[1].isEvent);
    CHECK(rows[1].event == "ArmDoors played");
}
