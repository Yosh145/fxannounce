// Phase 2 units without X-Plane: the SimBrief OFP, its check and the boarding schedule (VP_SPEC.md § 4.1a, § 4.2a),
// settings migration, trace compatibility, refuel detection and the probe checklist (docs/TOLISS_DATAREFS.md).
#include "core/config.h"
#include "core/probe.h"
#include "core/recorder.h"
#include "core/refuel.h"
#include "core/simbrief.h"

#include <doctest/doctest.h>

using namespace fxa;

namespace {

OfpText dlh400()
{
    OfpText t;
    t.seqno = 3;
    t.estOut = "1793452500";   // 2026-10-31 13:15:00 UTC
    t.icaoAirline = "DLH";
    t.flightNumber = "400";
    t.origin = "eddm";
    t.destination = "EGLL";
    t.paxCount = "150";
    t.maxPassengers = "180";
    t.aircraftIcao = "A20N";
    return t;
}

} // namespace

TEST_CASE("simbrief: OFP parsing")
{
    std::string why;
    const auto o = parseOfp(dlh400(), &why);
    REQUIRE(o);
    CHECK(why.empty());
    CHECK(o->callsign == "DLH400");
    CHECK(o->origin == "EDDM");
    CHECK(o->pax == 150);
    CHECK(o->seats == 180);
    CHECK(secondOfDay(static_cast<double>(o->estOut)) == doctest::Approx(13 * 3600 + 15 * 60));

    OfpText none;
    CHECK(!parseOfp(none, &why));
    CHECK(why.find("no OFP") != std::string::npos);
    OfpText bad = dlh400();
    bad.estOut = "13:15";
    CHECK(!parseOfp(bad, &why));
    CHECK(why.find("ETD") != std::string::npos);
    OfpText noNumbers = dlh400();
    noNumbers.paxCount = "";
    noNumbers.maxPassengers = "lots";
    noNumbers.flightNumber = "";
    const auto p = parseOfp(noNumbers);
    REQUIRE(p);
    CHECK(p->pax == -1);
    CHECK(p->seats == -1);
    CHECK(p->callsign.empty());
}

TEST_CASE("simbrief: time of day, wrapped around midnight")
{
    CHECK(secondOfDay(86400 + 60) == doctest::Approx(60));
    CHECK(secondOfDay(-60) == doctest::Approx(86340));
    CHECK(secondsUntil(13 * 3600, 12 * 3600) == doctest::Approx(3600));
    CHECK(secondsUntil(12 * 3600, 13 * 3600) == doctest::Approx(-3600));
    CHECK(secondsUntil(300, 86100) == doctest::Approx(600));      // 00:05 is 10 min after 23:55
    CHECK(secondsUntil(86100, 300) == doctest::Approx(-600));
    CHECK(zuluLabel(13 * 3600 + 15 * 60) == "13:15Z");
    CHECK(zuluLabel(-60) == "23:59Z");
}

TEST_CASE("simbrief: lead time by type and load (D-19)")
{
    Settings set;
    CHECK(boardingLead(Family::A320, 0, -1, set, 0) == doctest::Approx(30 * 60));
    CHECK(boardingLead(Family::A320, 180, -1, set, 0) == doctest::Approx(37 * 60));
    CHECK(boardingLead(Family::A320, 150, 180, set, 0) == doctest::Approx((30 + 7 * 150.0 / 180) * 60));
    CHECK(boardingLead(Family::A321, 110, -1, set, 0) == doctest::Approx(37 * 60));   // half of 220 seats
    CHECK(boardingLead(Family::A319, 500, -1, set, 0) == doctest::Approx(34 * 60));   // load clamped to 1
    CHECK(boardingLead(Family::A319, 0, -1, set, -60) == doctest::Approx(29 * 60));   // jitter
    CHECK(boardingLead(Family::A320, 0, -1, set, -1e6) == 0);                         // never negative
    set.a321LeadMin = 50;   // swapped range still works
    set.a321LeadMax = 40;
    CHECK(leadRange(Family::A321, set).minS == doctest::Approx(40 * 60));
}

TEST_CASE("simbrief: OFP check and boarding plan")
{
    Settings set;
    const Ofp o = *parseOfp(dlh400());
    // EDDM reference point and an aircraft at a gate 1.5 NM away.
    CHECK(checkOfp(o, true, 48.3538, 11.7861, 48.3530, 11.7480, set).empty());
    const std::string far = checkOfp(o, true, 48.3538, 11.7861, 50.0333, 8.5706, set);   // at EDDF
    CHECK(far.find("NM away") != std::string::npos);
    CHECK(checkOfp(o, false, 0, 0, 48.35, 11.78, set).find("not in X-Plane") != std::string::npos);
    Ofp noOrigin = o;
    noOrigin.origin.clear();
    CHECK(!checkOfp(noOrigin, true, 0, 0, 0, 0, set).empty());
    CHECK(distanceNm(0, 0, 1, 0) == doctest::Approx(60.0).epsilon(0.01));

    // ToLiss pax first, then the OFP's.
    const BoardingPlan p = makeBoardingPlan(o, Family::A320, 0, set, 0);
    CHECK(p.valid);
    CHECK(p.seqno == 3);
    CHECK(p.etdSec == doctest::Approx(13 * 3600 + 15 * 60));
    CHECK(p.boardingAtSec == doctest::Approx(13 * 3600 + 15 * 60 - (30 + 7 * 150.0 / 180) * 60));
    CHECK(p.summary.find("DLH400 EDDM-EGLL") != std::string::npos);
    const BoardingPlan full = makeBoardingPlan(o, Family::A320, 180, set, 0);
    CHECK(full.boardingAtSec == doctest::Approx(13 * 3600 + 15 * 60 - 37 * 60));
}

TEST_CASE("settings: boarding start names and the 0.1.x key")
{
    Settings s;
    CHECK(s.boarding() == BoardingStart::Schedule);
    loadSettings(Ini::parse("[cabin]\nboarding_start = call\n"), s);
    CHECK(s.boarding() == BoardingStart::Call);
    Ini out;
    storeSettings(s, out);
    CHECK(*out.get("cabin", "boarding_start") == "call");

    Settings old1;
    loadSettings(Ini::parse("[cabin]\nboarding_on_call = no\n"), old1);
    CHECK(old1.boarding() == BoardingStart::Doors);
    Settings old2;
    loadSettings(Ini::parse("[cabin]\nboarding_on_call = yes\n"), old2);   // the 0.1.x default
    CHECK(old2.boarding() == BoardingStart::Schedule);
    Settings both;
    loadSettings(Ini::parse("[cabin]\nboarding_on_call = no\nboarding_start = schedule\n"), both);
    CHECK(both.boarding() == BoardingStart::Schedule);

    Settings r;
    loadSettings(Ini::parse("[timing]\nrefuel_rate_kgs = 40\nrefuel_max_kgs = 10\n"), r);
    CHECK(r.refuelMaxKgs >= r.refuelRateKgs);
}

TEST_CASE("recorder: 0.1.x traces (Run0 layout) still parse, new columns round-trip")
{
    const std::string old =
        "kind,t,paused,replay,on_ground,gs_kt,ias_kt,agl_ft,msl_ft,vs_fpm,lat,lon,fmgs_phase_raw,cruise_alt_ft,beacon,"
        "seatbelt,landing_lights,speedbrake,park_brake,chocks,engines_running,engine_count,door_max,door_fwd,door_count,"
        "slides_armed,pax,fuel_kg,ac_power,pax_masks,cabin_alt_ft,pa_on,pa_vol,cab_on,cab_vol,cockpit_door,view_external,"
        "camera_in_cabin,sun_pitch,local_time,flight_number,acf_icao,satcom,event\n"
        "S,8.86,0,0,1,0.8,-0.1,-0,1471,-1,48.352232,11.777046,0,0,0,0,0,0.00,1,1,0,2,1.00,1.00,20,0,121,12084.8,0,0,0,0,0.00,0,0.00,0.00,0,0,12.9,55182,,A20N,0,\n"
        "E,112.69,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,BoardingWelcome playing: DLH/BoardingWelcome[Afternoon].ogg\n";
    const auto rows = readTrace(old);
    REQUIRE(rows.size() == 2);
    const SimState& a = rows[0].state;
    CHECK(a.onGround);
    CHECK(a.pax == 121);
    CHECK(a.doorFwd == doctest::Approx(1.0f));
    CHECK(a.fuelKg == doctest::Approx(12084.8f));
    CHECK(a.acfIcao == "A20N");
    CHECK(a.ecamPhase == -1);         // column absent: unknown
    CHECK(a.zuluSec == doctest::Approx(-1));
    CHECK(!a.throttleKnown());
    CHECK(rows[1].isEvent);
    CHECK(rows[1].event == "BoardingWelcome playing: DLH/BoardingWelcome[Afternoon].ogg");

    SimState s;
    s.t = 3;
    s.ecamPhase = 3;
    s.qpacPhase = 4;
    s.throttleMax = 0.88f;
    s.groundSpoilers = 0;
    s.zuluSec = 47700;
    s.tolissVolInternal = 0.5f;
    s.tolissVolOverride = 1;
    s.doorBlend = 0.25f;
    const auto back = readTrace(std::string(traceHeader()) + "\n" + traceLine(s) + "\n");
    REQUIRE(back.size() == 1);
    const SimState& b = back[0].state;
    CHECK(b.ecamPhase == 3);
    CHECK(b.qpacPhase == 4);
    CHECK(b.throttleMax == doctest::Approx(0.88f));
    CHECK(b.groundSpoilers == 0);
    CHECK(b.zuluSec == doctest::Approx(47700));
    CHECK(b.tolissVolInternal == doctest::Approx(0.5f));
    CHECK(b.tolissVolOverride == 1);
    CHECK(b.doorBlend == doctest::Approx(0.25f));
}

TEST_CASE("refuel detection ignores instant ISCS loads")
{
    Settings set;
    RefuelDetector d;
    SimState s;
    s.onGround = true;
    s.fuelKg = 5000;
    for (int k = 0; k <= 4; ++k) {   // an instant +7000 kg between two samples
        s.t = k;
        if (k == 2) s.fuelKg = 12000;
        d.update(s, set);
    }
    CHECK(!d.refueling(s.t));
    double detected = -1;
    for (int k = 5; k <= 20; ++k) {   // a real refuel at 20 kg/s
        s.t = k;
        s.fuelKg += 20;
        const double r = d.update(s, set);
        if (r > 0) detected = r;
    }
    CHECK(detected == doctest::Approx(20).epsilon(0.05));
    CHECK(d.refueling(s.t));
    CHECK(!d.refueling(s.t + 61));
    s.onGround = false;   // airborne: never refuelling
    RefuelDetector air;
    for (int k = 0; k <= 5; ++k) {
        s.t = k;
        s.fuelKg += 20;
        air.update(s, set);
    }
    CHECK(!air.refueling(s.t));
}

TEST_CASE("probe: sightings, guided steps, census result")
{
    ProbeChecklist p;
    SimState s;
    ProbeRaw raw;
    raw.acpKnob[0] = std::vector<float>(12, 0.0f);
    raw.landingCandidates = { 0, 0, 0 };
    raw.landingNames = { "a", "b", "c" };
    auto find = [&](const char* id) {
        for (const ProbeItem& it : p.items())
            if (it.id == id) return it;
        return ProbeItem{};
    };
    const int before = p.doneCount();

    // APPhase: needs 0-5 and 7 (GA optional); context of each first sighting.
    for (int v : { 0, 1, 2, 3, 4, 5 }) {
        s.fmgsPhaseRaw = v;
        p.update(s, raw);
    }
    CHECK(!find("apphase").done);
    s.fmgsPhaseRaw = 7;
    s.onGround = true;
    s.gsKt = 20;
    p.update(s, raw);
    CHECK(find("apphase").done);
    CHECK(find("apphase").finding.find("7 (ground, 20 kt") != std::string::npos);

    // ACP step: the toggled index becomes the Apply value.
    s.t = 100;
    p.startStep(ProbeStep::AcpCab, s, raw);
    CHECK(p.activeStep() == ProbeStep::AcpCab);
    s.t = 105;
    raw.acpKnob[0][6] = 1;
    p.update(s, raw);
    CHECK(p.activeStep() == ProbeStep::None);
    CHECK(find("acp_cab").done);
    CHECK(find("acp_cab").applyKey == "acp_cab_index");
    CHECK(find("acp_cab").applyValue == "6");

    // A step with nothing moving times out.
    s.t = 200;
    p.startStep(ProbeStep::LandingLights, s, raw);
    s.t = 230;
    p.update(s, raw);
    CHECK(p.activeStep() == ProbeStep::None);
    CHECK(!find("landing").done);
    s.t = 300;
    p.startStep(ProbeStep::LandingLights, s, raw);
    raw.landingCandidates[2] = 1;
    s.t = 302;
    p.update(s, raw);
    CHECK(find("landing").applyValue == "2");

    // Volume step: waits for the census, prefers volume-like names.
    s.t = 400;
    p.startStep(ProbeStep::VolumeOverride, s, raw);
    CHECK(!p.wantsCensusDiff(410));
    CHECK(p.wantsCensusDiff(421));
    p.censusResult({ "AirbusFBW/SomeTimer", "AirbusFBW/OvrdXPVolumes" });
    CHECK(find("volume").done);
    CHECK(find("volume").applyValue == "AirbusFBW/OvrdXPVolumes");
    CHECK(p.activeStep() == ProbeStep::None);

    CHECK(p.doneCount() > before);
    const std::string md = p.summary("### test");
    CHECK(md.find("| Item | Done | Seen |") != std::string::npos);
    CHECK(md.find("APPhase sequence: 0 > 1 > 2 > 3 > 4 > 5 > 7") != std::string::npos);
    p.reset();
    CHECK(p.doneCount() == 0);
    CHECK(!find("apphase").done);
}
