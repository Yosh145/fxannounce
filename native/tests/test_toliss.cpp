// phase 2 stuff w/o xp ofp check boarding schedule 4.1a 4.2a settings migration
// 0.3.0 ui keys trace compat refuel
#include <doctest/doctest.h>

#include "core/config.h"
#include "core/recorder.h"
#include "core/refuel.h"
#include "core/simbrief.h"
#include "core/toliss_decode.h"

using namespace fxa;

namespace {

OfpText dlh400() {
  OfpText t;
  t.seqno = 3;
  t.estOut = "1793452500";  // 2026-10-31 13:15z
  t.icaoAirline = "DLH";
  t.flightNumber = "400";
  t.origin = "eddm";
  t.destination = "EGLL";
  t.paxCount = "150";
  t.maxPassengers = "180";
  t.aircraftIcao = "A20N";
  return t;
}

}  // namespace

TEST_CASE("simbrief: OFP parsing") {
  std::string why;
  const auto o = parseOfp(dlh400(), &why);
  REQUIRE(o);
  CHECK(why.empty());
  CHECK(o->callsign == "DLH400");
  CHECK(o->origin == "EDDM");
  CHECK(o->pax == 150);
  CHECK(o->seats == 180);
  CHECK(secondOfDay(static_cast<double>(o->estOut)) ==
        doctest::Approx(13 * 3600 + 15 * 60));

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

TEST_CASE("simbrief: time of day, wrapped around midnight") {
  CHECK(secondOfDay(86400 + 60) == doctest::Approx(60));
  CHECK(secondOfDay(-60) == doctest::Approx(86340));
  CHECK(secondsUntil(13 * 3600, 12 * 3600) == doctest::Approx(3600));
  CHECK(secondsUntil(12 * 3600, 13 * 3600) == doctest::Approx(-3600));
  CHECK(secondsUntil(300, 86100) ==
        doctest::Approx(600));  // 00:05 is 10 min after 23:55
  CHECK(secondsUntil(86100, 300) == doctest::Approx(-600));
  CHECK(zuluLabel(13 * 3600 + 15 * 60) == "13:15Z");
  CHECK(zuluLabel(-60) == "23:59Z");
}

TEST_CASE("simbrief: lead time by type and load (D-19)") {
  Settings set;
  CHECK(boardingLead(Family::A320, 0, -1, set, 0) == doctest::Approx(30 * 60));
  CHECK(boardingLead(Family::A320, 180, -1, set, 0) ==
        doctest::Approx(37 * 60));
  CHECK(boardingLead(Family::A320, 150, 180, set, 0) ==
        doctest::Approx((30 + 7 * 150.0 / 180) * 60));
  CHECK(boardingLead(Family::A321, 110, -1, set, 0) ==
        doctest::Approx(37 * 60));  // half of 220
  CHECK(boardingLead(Family::A319, 500, -1, set, 0) ==
        doctest::Approx(34 * 60));  // clamped to 1
  CHECK(boardingLead(Family::A319, 0, -1, set, -60) ==
        doctest::Approx(29 * 60));
  CHECK(boardingLead(Family::A320, 0, -1, set, -1e6) == 0);
  set.a321LeadMin = 50;  // swapped range ok
  set.a321LeadMax = 40;
  CHECK(leadRange(Family::A321, set).minS == doctest::Approx(40 * 60));
}

TEST_CASE("simbrief: OFP check and boarding plan") {
  Settings set;
  const Ofp o = *parseOfp(dlh400());
  // eddm ref point aircraft at a gate 1.5 nm out
  CHECK(checkOfp(o, true, 48.3538, 11.7861, 48.3530, 11.7480, set).empty());
  const std::string far =
      checkOfp(o, true, 48.3538, 11.7861, 50.0333, 8.5706, set);  // at eddf
  CHECK(far.find("NM away") != std::string::npos);
  CHECK(checkOfp(o, false, 0, 0, 48.35, 11.78, set).find("not in X-Plane") !=
        std::string::npos);
  Ofp noOrigin = o;
  noOrigin.origin.clear();
  CHECK(!checkOfp(noOrigin, true, 0, 0, 0, 0, set).empty());
  CHECK(distanceNm(0, 0, 1, 0) == doctest::Approx(60.0).epsilon(0.01));

  // toliss pax wins
  const BoardingPlan p = makeBoardingPlan(o, Family::A320, 0, set, 0);
  CHECK(p.valid);
  CHECK(p.seqno == 3);
  CHECK(p.etdSec == doctest::Approx(13 * 3600 + 15 * 60));
  CHECK(p.boardingAtSec ==
        doctest::Approx(13 * 3600 + 15 * 60 - (30 + 7 * 150.0 / 180) * 60));
  CHECK(p.summary.find("DLH400 EDDM-EGLL") != std::string::npos);
  const BoardingPlan full = makeBoardingPlan(o, Family::A320, 180, set, 0);
  CHECK(full.boardingAtSec == doctest::Approx(13 * 3600 + 15 * 60 - 37 * 60));
}

TEST_CASE(
    "simbrief: the OFP through the flight: latched at takeoff, en route, "
    "returning, arrived (VP_SPEC 4.2a)") {
  Settings set;
  const Ofp o = *parseOfp(dlh400());  // dlh400 eddm-egll seqno 3
  const OfpPlace eddm{true, 48.3538, 11.7861}, egll{true, 51.4706, -0.4619};
  OfpTracker tr;
  // gate origin check
  tr.update(o, eddm, egll, 48.3530, 11.7480, true, false, set);
  CHECK(tr.usable());
  CHECK(!tr.latched());
  CHECK(tr.stage() == OfpTracker::Stage::AtOrigin);
  // takeoff latched
  tr.update(o, eddm, egll, 48.36, 11.70, true, true, set);
  CHECK(tr.latched());
  CHECK(tr.currentIsLatched());
  CHECK(!tr.usable());
  // cruising over fra 209 nm out en route not ignored
  tr.update(o, eddm, egll, 50.0333, 8.5706, false, true, set);
  CHECK(tr.stage() == OfpTracker::Stage::EnRoute);
  CHECK(tr.why().empty());
  CHECK(tr.status().find("DLH400 EDDM-EGLL: en route to EGLL, ") == 0);
  // turning back 10 nm closer to eddm and 10 farther from egll
  tr.update(o, eddm, egll, 49.6, 9.8, false, true, set);
  CHECK(tr.stage() == OfpTracker::Stage::Returning);
  CHECK(tr.status().find("returning to EDDM") != std::string::npos);
  // back at eddm passes again
  tr.update(o, eddm, egll, 48.3530, 11.7480, true, true, set);
  CHECK(tr.stage() == OfpTracker::Stage::AtOrigin);
  CHECK(tr.usable());

  OfpTracker t2;  // normal flight next legs ofp loaded in flight
  t2.update(o, eddm, egll, 48.3530, 11.7480, true, true, set);
  REQUIRE(t2.latched());
  t2.update(o, eddm, egll, 50.0333, 8.5706, false, true, set);
  Ofp next = o;
  next.seqno = 4;
  next.origin = "EGLL";
  next.destination = "EDDM";
  next.callsign = "DLH401";
  t2.update(next, egll, eddm, 50.5, 5.0, false, true, set);
  CHECK(!t2.usable());
  CHECK(t2.status().find("next OFP DLH401 EGLL-EDDM") != std::string::npos);
  t2.update(next, egll, eddm, 51.4710, -0.4500, true, true, set);
  CHECK(t2.stage() == OfpTracker::Stage::Arrived);
  CHECK(t2.status().find("arrived at EGLL") != std::string::npos);
  CHECK(t2.usable());  // next leg passes at its origin
  t2.update(next, egll, eddm, 51.4710, -0.4500, true, false,
            set);  // purser starts the next flight
  CHECK(!t2.latched());
  CHECK(t2.usable());

  OfpTracker t3;  // same ofp after the flight the one just flown
  t3.update(o, eddm, egll, 48.3530, 11.7480, true, true, set);
  t3.update(o, eddm, egll, 51.4710, -0.4500, true, true, set);
  t3.update(o, eddm, egll, 51.4710, -0.4500, true, false, set);
  CHECK(!t3.usable());
  CHECK(t3.why().find("the flight just flown") != std::string::npos);
}

TEST_CASE(
    "simbrief: a departure route looping back over the airport is not a "
    "return") {
  Settings set;
  const Ofp o = *parseOfp(dlh400());
  const OfpPlace eddm{true, 48.3538, 11.7861}, egll{true, 51.4706, -0.4619};
  OfpTracker tr;
  tr.update(o, eddm, egll, 48.3530, 11.7480, true, true, set);
  tr.update(o, eddm, egll, 48.35, 12.30, false, true,
            set);  // east first 21 nm out
  tr.update(o, eddm, egll, 48.36, 11.75, false, true,
            set);  // back over the field heading west
  CHECK(tr.stage() ==
        OfpTracker::Stage::EnRoute);  // egll closer than ever not returning
}

TEST_CASE(
    "ToLiss decoders: SDSpoilerArray status codes and the strobe switch "
    "(Run2)") {
  // run2 values unpowered hydraulics up roll spoilers in a turn ground spoilers
  // at touchdown
  CHECK(groundSpoilersFromSd(std::vector<float>(16, 2.0f)) == -1);
  CHECK(groundSpoilersFromSd(
            {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 2, 2}) == 0);
  CHECK(groundSpoilersFromSd(
            {0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 2, 2, 2, 2, 2, 2}) == 0);
  CHECK(groundSpoilersFromSd(
            {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2}) == 1);
  CHECK(groundSpoilersFromSd({}) == -1);
  // strobe 7 1 auto 2 on
  const std::vector<float> taxi = {1, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 2};
  const std::vector<float> lineup = {1, 0, 1, 2, 2, 2, 1, 2, 0, 0, 1, 1, 2};
  CHECK(switchAt(taxi, 7, 2) == 0);
  CHECK(switchAt(lineup, 7, 2) == 1);
  CHECK(switchAt(lineup, 40, 2) == -1);
}

TEST_CASE("settings: boarding start names and the 0.1.x key") {
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
  loadSettings(Ini::parse("[cabin]\nboarding_on_call = yes\n"),
               old2);  // 0.1.x default
  CHECK(old2.boarding() == BoardingStart::Schedule);
  Settings both;
  loadSettings(
      Ini::parse("[cabin]\nboarding_on_call = no\nboarding_start = schedule\n"),
      both);
  CHECK(both.boarding() == BoardingStart::Schedule);

  Settings r;
  loadSettings(
      Ini::parse("[timing]\nrefuel_rate_kgs = 40\nrefuel_max_kgs = 10\n"), r);
  CHECK(r.refuelMaxKgs >= r.refuelRateKgs);
}

TEST_CASE(
    "settings: 0.2.0 defaults are migrated once (PA index 7 -> 15, closed-door "
    "floor 0.25 -> 0.1)") {
  // 0.2.0 file every key written no settings_version
  const Ini old = Ini::parse(
      "[hearing]\nfloor = 0.25\n[toliss]\nacp_pa_index = 7\nacp_cab_index = "
      "6\n");
  Settings s;
  loadSettings(old, s);
  CHECK(s.acpPaIndex == 7);  // loadsettings alone never migrates
  const std::vector<std::string> notes = migrateSettings(old, s);
  CHECK(notes.size() == 2);
  CHECK(s.acpPaIndex == 15);
  CHECK(s.hearingFloor == doctest::Approx(0.10f));
  CHECK(s.acpCabIndex == 6);
  Ini out;
  storeSettings(s, out);
  CHECK(*out.get("general", "settings_version") ==
        std::to_string(kSettingsVersion));

  // user picks are kept
  const Ini mine =
      Ini::parse("[hearing]\nfloor = 0.3\n[toliss]\nacp_pa_index = 12\n");
  Settings m;
  loadSettings(mine, m);
  CHECK(migrateSettings(mine, m).empty());
  CHECK(m.acpPaIndex == 12);
  CHECK(m.hearingFloor == doctest::Approx(0.3f));

  // current files arent touched again even w 7
  const Ini now = Ini::parse(
      "[general]\nsettings_version = 2\n[toliss]\nacp_pa_index = 7\n");
  Settings n;
  loadSettings(now, n);
  CHECK(migrateSettings(now, n).empty());
  CHECK(n.acpPaIndex == 7);

  Settings d;
  CHECK(migrateSettings(Ini::parse(""), d).empty());
  CHECK(d.acpPaIndex == 15);
  CHECK(d.hearingFloor == doctest::Approx(0.10f));
  CHECK(d.muffle);
}

TEST_CASE(
    "recorder: 0.1.x traces (Run0 layout) still parse, new columns "
    "round-trip") {
  const std::string old =
      "kind,t,paused,replay,on_ground,gs_kt,ias_kt,agl_ft,msl_ft,vs_fpm,lat,"
      "lon,fmgs_phase_raw,cruise_alt_ft,beacon,"
      "seatbelt,landing_lights,speedbrake,park_brake,chocks,engines_running,"
      "engine_count,door_max,door_fwd,door_count,"
      "slides_armed,pax,fuel_kg,ac_power,pax_masks,cabin_alt_ft,pa_on,pa_vol,"
      "cab_on,cab_vol,cockpit_door,view_external,"
      "camera_in_cabin,sun_pitch,local_time,flight_number,acf_icao,satcom,"
      "event\n"
      "S,8.86,0,0,1,0.8,-0.1,-0,1471,-1,48.352232,11.777046,0,0,0,0,0,0.00,1,1,"
      "0,2,1.00,1.00,20,0,121,12084.8,0,0,0,0,0.00,0,0.00,0.00,0,0,12.9,55182,,"
      "A20N,0,\n"
      "E,112.69,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,BoardingWelcome "
      "playing: DLH/BoardingWelcome[Afternoon].ogg\n";
  const auto rows = readTrace(old);
  REQUIRE(rows.size() == 2);
  const SimState& a = rows[0].state;
  CHECK(a.onGround);
  CHECK(a.pax == 121);
  CHECK(a.doorFwd == doctest::Approx(1.0f));
  CHECK(a.fuelKg == doctest::Approx(12084.8f));
  CHECK(a.acfIcao == "A20N");
  CHECK(a.ecamPhase == -1);  // no column unknown
  CHECK(a.zuluSec == doctest::Approx(-1));
  CHECK(!a.throttleKnown());
  CHECK(rows[1].isEvent);
  CHECK(rows[1].event ==
        "BoardingWelcome playing: DLH/BoardingWelcome[Afternoon].ogg");

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
  s.strobeOn = 1;
  s.noSmoking = 0;
  s.gearLeverDown = 1;
  const auto back =
      readTrace(std::string(traceHeader()) + "\n" + traceLine(s) + "\n");
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
  CHECK(b.strobeOn == 1);
  CHECK(b.noSmoking == 0);
  CHECK(b.gearLeverDown == 1);
  CHECK(a.strobeOn == -1);  // older traces
}

TEST_CASE("refuel detection ignores instant ISCS loads") {
  Settings set;
  RefuelDetector d;
  SimState s;
  s.onGround = true;
  s.fuelKg = 5000;
  for (int k = 0; k <= 4; ++k) {  // instant 7000 kg
    s.t = k;
    if (k == 2) s.fuelKg = 12000;
    d.update(s, set);
  }
  CHECK(!d.refueling(s.t));
  double detected = -1;
  for (int k = 5; k <= 20; ++k) {  // real refuel 20 kg/s
    s.t = k;
    s.fuelKg += 20;
    const double r = d.update(s, set);
    if (r > 0) detected = r;
  }
  CHECK(detected == doctest::Approx(20).epsilon(0.05));
  CHECK(d.refueling(s.t));
  CHECK(!d.refueling(s.t + 61));
  s.onGround = false;  // airborne never refuelling
  RefuelDetector air;
  for (int k = 0; k <= 5; ++k) {
    s.t = k;
    s.fuelKg += 20;
    air.update(s, set);
  }
  CHECK(!air.refueling(s.t));
}

TEST_CASE(
    "settings: 0.3.0 window keys, the Inspector tab id and the Settings pages "
    "(ARCHITECTURE 7)") {
  Settings s;
  CHECK(s.theme == 0);
  CHECK(s.showTimeline);
  CHECK(!s.showLog);
  s.theme = 1;
  s.showTimeline = false;
  s.showLog = true;
  s.fontScale = 1.4f;
  Ini ini;
  storeSettings(s, ini);
  CHECK(*ini.get("ui", "theme") == "white");
  Settings back;
  loadSettings(ini, back);
  CHECK(back.theme == 1);
  CHECK(!back.showTimeline);
  CHECK(back.showLog);
  CHECK(back.fontScale == doctest::Approx(1.4f));  // 0.2.x never read this back

  Settings t;
  loadSettings(Ini::parse("[ui]\ntab = 4\n"), t);  // old inspector id
  CHECK(t.tab == 0);
  loadSettings(Ini::parse("[ui]\ntab = 5\n"), t);
  CHECK(t.tab == 5);

  int perPage[kSettingPageCount] = {};
  for (const SettingDef& d : settingDefs()) {
    if (d.hidden) continue;
    const int k = static_cast<int>(d.page);
    REQUIRE(k >= 0);
    REQUIRE(k < kSettingPageCount);
    ++perPage[k];
    CHECK_MESSAGE(std::string(d.help).find("Inspector") == std::string::npos,
                  d.key);
    CHECK_MESSAGE(std::string(d.help).find("probe") == std::string::npos,
                  d.key);
  }
  for (int k = 0; k < kSettingPageCount; ++k)
    CHECK_MESSAGE(perPage[k] > 0, settingPageName(static_cast<SettingPage>(k)));
  auto pageOf = [](const char* key) {
    for (const SettingDef& d : settingDefs())
      if (std::string(d.key) == key) return d.page;
    return SettingPage::Count;
  };
  CHECK(pageOf("enabled") == SettingPage::Announcements);
  CHECK(pageOf("master_volume") == SettingPage::Sound);
  CHECK(pageOf("pa_bus") == SettingPage::Advanced);
  CHECK(pageOf("show_log") == SettingPage::Advanced);
  CHECK(pageOf("welcome_min_s") == SettingPage::Boarding);
  CHECK(pageOf("expedite_s") == SettingPage::Timing);
  CHECK(pageOf("theme") == SettingPage::Display);
}
