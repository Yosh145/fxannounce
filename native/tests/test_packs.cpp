// packs filename parsing legacy names fenix tag selection airline table
// resolution last cases use /packs when its there never committed
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "core/airlines.h"
#include "core/pack_library.h"
#include "core/pack_resolver.h"
#include "core/selector.h"

using namespace fxa;

namespace {

Pack makePack(const std::string& name,
              const std::vector<std::string>& fileNames) {
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

SelectContext ctxFor(Family fam, bool neo, TimeOfDay t) {
  SelectContext c;
  c.aircraft = makeAircraftType(fam, neo);
  c.time = t;
  return c;
}

std::string pick(const Pack& p, AnnType t, const SelectContext& c,
                 unsigned seed = 1) {
  Rng rng(seed);
  const Selection s = selectSound(p, t, c, rng);
  return s.file ? s.file->relPath : std::string("(none)");
}

std::string packsDir() { return std::string(FXA_SOURCE_DIR) + "/../Packs"; }

}  // namespace

TEST_CASE("file names: type, tags, display name") {
  auto f = parseSoundFileName("BoardingWelcome[A321][Morning][1].ogg");
  REQUIRE(f);
  CHECK(f->type == AnnType::BoardingWelcome);
  CHECK(f->aircraft == std::vector<std::string>{"A321"});
  CHECK(f->times.size() == 1);
  CHECK(f->variants == std::vector<int>{1});
  CHECK(f->displayName == "BoardingWelcome[A321][Morning][1]");

  f = parseSoundFileName("BoardingWelcome[Prerecorded]~Welcome passengers.ogg");
  REQUIRE(f);
  CHECK(f->mode == Mode::Prerecorded);
  CHECK(f->displayName == "Welcome passengers");

  // tags after belong to the display name fenix calls it incorrect
  f = parseSoundFileName("BoardingWelcome~Welcome passengers[Prerecorded].ogg");
  REQUIRE(f);
  CHECK(!f->mode);
  CHECK(f->displayName == "Welcome passengers[Prerecorded]");

  f = parseSoundFileName("BoardingWelcome[A319][USB][Refuelling][Night].ogg");
  REQUIRE(f);
  CHECK(f->refuel);
  CHECK(f->custom == std::vector<std::string>{"USB"});

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

TEST_CASE("legacy names map to Fenix types with a call context") {
  auto f = parseSoundFileName("CrewSeatsLanding[1].ogg");
  REQUIRE(f);
  CHECK(f->type == AnnType::CrewTakeSeats);
  CHECK(f->context == CallContext::Landing);
  CHECK(f->legacy);
  CHECK(parseSoundFileName("CallCabinSecureTakeoff.ogg")->type ==
        AnnType::CabinSecured);
  CHECK(parseSoundFileName("CallCabinSecureTakeoff.ogg")->effectiveMode() ==
        Mode::Interphone);
  CHECK(parseSoundFileName("DescentSeatbelt.ogg")->type ==
        AnnType::DescentStarted);  // ejus spelling
  CHECK(parseSoundFileName("DescentSeatbelts.ogg")->type ==
        AnnType::DescentStarted);
  CHECK(parseSoundFileName("FastenSeatbelt.ogg")->type ==
        AnnType::SeatbeltSignOn);
  CHECK(parseSoundFileName("DisembarkStarted.ogg")->type ==
        AnnType::DeboardingStarted);
  CHECK(parseSoundFileName("SafetyBriefing.ogg")->effectiveMode() ==
        Mode::Prerecorded);
  CHECK(parseSoundFileName("ArmDoors.ogg")->effectiveMode() == Mode::Manual);
}

TEST_CASE(
    "names from other announcer tools stay custom, with a rename hint (VP_SPEC "
    "1.2, public 0.2.0 issue)") {
  // not fenix types or legacy names checked against the guides fcac legacy list
  CHECK(!parseSoundFileName("BoardingStarted.ogg")->type);
  CHECK(!parseSoundFileName("Turbulence.ogg")->type);
  CHECK(std::string(fenixRenameHint("BoardingStarted")) == "BoardingWelcome");
  CHECK(std::string(fenixRenameHint("turbulence")) == "SeatbeltSignOn");
  CHECK(fenixRenameHint("GalleyClosed") == nullptr);

  namespace fs = std::filesystem;
  const fs::path root = fs::temp_directory_path() / "fxa_test_custom_packs";
  std::error_code ec;
  fs::remove_all(root, ec);
  fs::create_directories(root / "WZZ");
  for (const char* name :
       {"BoardingStarted.ogg", "Turbulence.ogg", "Turbulence[1].ogg",
        "GalleyClosed.ogg", "BoardingWelcome.ogg"})
    std::ofstream(root / "WZZ" / name) << "x";
  const PackLibrary lib = scanPacks({root.string()});
  fs::remove_all(root, ec);
  REQUIRE(lib.packs.size() == 1);
  const Pack& p = lib.packs[0];
  const std::vector<std::string> custom = p.customTypes();
  CHECK(custom == std::vector<std::string>{"BoardingStarted", "GalleyClosed",
                                           "Turbulence"});
  auto warned = [&](const std::string& text) {
    return std::any_of(p.warnings.begin(), p.warnings.end(),
                       [&](const std::string& w) {
                         return w.find(text) != std::string::npos;
                       });
  };
  CHECK(warned(
      "BoardingStarted.ogg: 'BoardingStarted' is not a Fenix announcement "
      "type, not played (rename to BoardingWelcome to use it)"));
  CHECK(
      warned("Turbulence.ogg: 'Turbulence' is not a Fenix announcement type, "
             "not played (rename to SeatbeltSignOn to use it)"));
  CHECK(
      warned("GalleyClosed.ogg: 'GalleyClosed' is not a Fenix announcement "
             "type, not played (FAP-only in Fenix)"));
  CHECK(p.countOf(AnnType::BoardingWelcome) == 1);
}

TEST_CASE("time of day spans") {
  CHECK(timeOfDayAt(0) == TimeOfDay::Night);
  CHECK(timeOfDayAt(5.99 * 3600) == TimeOfDay::Night);
  CHECK(timeOfDayAt(6 * 3600) == TimeOfDay::Morning);
  CHECK(timeOfDayAt(12 * 3600) == TimeOfDay::Afternoon);
  CHECK(timeOfDayAt(19 * 3600) == TimeOfDay::Evening);
  CHECK(timeOfDayAt(86400 + 3600) == TimeOfDay::Night);
}

TEST_CASE("selection: most matching tags wins, every category must hold") {
  const Pack dlh = makePack(
      "DLH", {"BoardingWelcome[Afternoon].ogg", "BoardingWelcome[Evening].ogg",
              "BoardingWelcome[Refueling][Evening].ogg",
              "BoardingWelcome[Refueling][Morning].ogg", "DescentSeatbelts.ogg",
              "DescentSeatbelts[Evening].ogg"});
  SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Evening);
  CHECK(pick(dlh, AnnType::BoardingWelcome, c) ==
        "BoardingWelcome[Evening].ogg");
  c.refueling = true;
  CHECK(pick(dlh, AnnType::BoardingWelcome, c) ==
        "BoardingWelcome[Refueling][Evening].ogg");
  c.time = TimeOfDay::Night;  // nothing for night d-28 nearest time
  c.localHour = 1.0;
  CHECK(pick(dlh, AnnType::BoardingWelcome, c) ==
        "BoardingWelcome[Refueling][Evening].ogg");
  c.localHour = 5.0;
  CHECK(pick(dlh, AnnType::BoardingWelcome, c) ==
        "BoardingWelcome[Refueling][Morning].ogg");
  // the untagged file fits at night so no time fallback
  CHECK(pick(dlh, AnnType::DescentStarted, c) == "DescentSeatbelts.ogg");
  c.time = TimeOfDay::Evening;
  CHECK(pick(dlh, AnnType::DescentStarted, c) ==
        "DescentSeatbelts[Evening].ogg");
}

TEST_CASE("selection: aircraft type tags are hierarchical") {
  const Pack ual =
      makePack("UAL", {"SafetyBriefing[A21N].ogg", "SafetyBriefing[A319].ogg",
                       "SafetyBriefing[A320].ogg", "SafetyBriefing[B738].ogg",
                       "PreSafetyBriefing.ogg", "ArmDoors[A321].ogg",
                       "ArmDoors[A21N].ogg"});
  CHECK(pick(ual, AnnType::SafetyBriefing,
             ctxFor(Family::A321, true, TimeOfDay::Morning)) ==
        "SafetyBriefing[A21N].ogg");
  CHECK(pick(ual, AnnType::SafetyBriefing,
             ctxFor(Family::A321, false, TimeOfDay::Morning)) == "(none)");
  CHECK(pick(ual, AnnType::SafetyBriefing,
             ctxFor(Family::A330, false, TimeOfDay::Morning)) == "(none)");
  CHECK(pick(ual, AnnType::SafetyBriefing,
             ctxFor(Family::A320, true, TimeOfDay::Morning)) ==
        "SafetyBriefing[A320].ogg");
  CHECK(pick(ual, AnnType::SafetyBriefing,
             ctxFor(Family::A319, false, TimeOfDay::Morning)) ==
        "SafetyBriefing[A319].ogg");
  // specific tag beats the family tag on a neo family still plays on the ceo
  for (unsigned seed = 1; seed < 20; ++seed)
    CHECK(pick(ual, AnnType::ArmDoors,
               ctxFor(Family::A321, true, TimeOfDay::Morning),
               seed) == "ArmDoors[A21N].ogg");
  CHECK(pick(ual, AnnType::ArmDoors,
             ctxFor(Family::A321, false, TimeOfDay::Morning)) ==
        "ArmDoors[A321].ogg");
}

TEST_CASE("selection: legacy context, custom tags, overwater, phases") {
  const Pack p =
      makePack("XXX", {"CrewSeatsTakeoff.ogg", "CrewSeatsLanding.ogg",
                       "CrewTakeSeats[Approach].ogg", "SafetyBriefing.ogg",
                       "SafetyBriefing[Overwater].ogg",
                       "SafetyBriefing[WiFi][USB].ogg"});
  SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Morning);
  c.context = CallContext::Takeoff;
  CHECK(pick(p, AnnType::CrewTakeSeats, c) == "CrewSeatsTakeoff.ogg");
  c.context = CallContext::Landing;
  c.phase = Phase::Descent;
  CHECK(pick(p, AnnType::CrewTakeSeats, c) == "CrewSeatsLanding.ogg");
  c.phase = Phase::Approach;
  // both score 1 either is fine per fenix
  const std::string landing = pick(p, AnnType::CrewTakeSeats, c);
  CHECK((landing == "CrewSeatsLanding.ogg" ||
         landing == "CrewTakeSeats[Approach].ogg"));

  CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing.ogg");
  c.overwater = true;
  CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing[Overwater].ogg");
  c.overwater = false;
  c.customTags = {"WiFi"};  // no usb so wifi usb is out
  CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing.ogg");
  c.customTags = {"wifi", "USB"};
  CHECK(pick(p, AnnType::SafetyBriefing, c) == "SafetyBriefing[WiFi][USB].ogg");
}

TEST_CASE("selection: variants and the variant 0 fallback") {
  const Pack ual =
      makePack("UAL", {"BoardingComplete.ogg", "BoardingComplete[1].ogg",
                       "AfterTakeoff[1].ogg", "ArmDoors.ogg"});
  CHECK(ual.maxVariant == 1);
  SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Morning);
  c.variant = 1;
  CHECK(pick(ual, AnnType::BoardingComplete, c) == "BoardingComplete[1].ogg");
  CHECK(pick(ual, AnnType::AfterTakeoff, c) == "AfterTakeoff[1].ogg");
  CHECK(pick(ual, AnnType::ArmDoors, c) ==
        "ArmDoors.ogg");  // no variant 1 file falls back to 0
  c.variant = 0;
  CHECK(pick(ual, AnnType::BoardingComplete, c) == "BoardingComplete.ogg");
  CHECK(pick(ual, AnnType::AfterTakeoff, c) ==
        "AfterTakeoff[1].ogg");  // only variant 1 exists d-27
  // boardingcomplete has 0 and 1 so 0 is a real choice
  CHECK(ual.variantChoices() == std::vector<int>{0, 1});
  Rng rng(7);
  bool saw[2] = {false, false};
  for (int k = 0; k < 50; ++k) {
    const int v = pickVariant(ual, rng);
    REQUIRE((v == 0 || v == 1));
    saw[v] = true;
  }
  CHECK((saw[0] && saw[1]));
}

TEST_CASE("selection: a BAW-style pack plays every call in every variant") {
  // the real baw pack: some calls only as [1]-[3] or [1]-[2], no untagged
  const Pack baw = makePack("BAW", {"AfterLanding[Afternoon].ogg",
                                    "AfterLanding[Evening].ogg",
                                    "AfterLanding[Morning].ogg",
                                    "AfterTakeoff[Afternoon][1].ogg",
                                    "AfterTakeoff[Afternoon][2].ogg",
                                    "AfterTakeoff[Evening][1].ogg",
                                    "AfterTakeoff[Evening][2].ogg",
                                    "AfterTakeoff[Morning][1].ogg",
                                    "AfterTakeoff[Morning][2].ogg",
                                    "ArmDoors[1].ogg",
                                    "ArmDoors[2].ogg",
                                    "ArmDoors[3].ogg",
                                    "BoardingComplete[1].ogg",
                                    "BoardingComplete[2].ogg",
                                    "BoardingComplete[3].ogg",
                                    "BoardingWelcome[Afternoon].ogg",
                                    "BoardingWelcome[Evening].ogg",
                                    "BoardingWelcome[Morning].ogg",
                                    "BoardingWelcome[Refueling].ogg",
                                    "CabinDimTakeoff.ogg",
                                    "CallCabinSecureLanding.ogg",
                                    "CallCabinSecureTakeoff.ogg",
                                    "CrewSeatsLanding.ogg",
                                    "CrewSeatsTakeoff.ogg",
                                    "DescentSeatbelts.ogg",
                                    "DisarmDoors[1].ogg",
                                    "DisarmDoors[2].ogg",
                                    "DisarmDoors[3].ogg",
                                    "DisembarkStarted.ogg",
                                    "FastenSeatbelt.ogg",
                                    "PreSafetyBriefing[1][A319].ogg",
                                    "PreSafetyBriefing[1][A320].ogg",
                                    "PreSafetyBriefing[1][A321].ogg",
                                    "PreSafetyBriefing[2][A319].ogg",
                                    "PreSafetyBriefing[2][A320].ogg",
                                    "PreSafetyBriefing[2][A321].ogg",
                                    "SafetyBriefing.ogg"});
  for (const SoundFile& f : baw.files) CHECK(f.type);

  // 3.4 d-27 never 0 and every real variant comes up
  CHECK(baw.variantChoices() == std::vector<int>{1, 2, 3});
  Rng rng(11);
  bool saw[4] = {false, false, false, false};
  for (int k = 0; k < 200; ++k) {
    const int v = pickVariant(baw, rng);
    REQUIRE((v >= 1 && v <= 3));
    saw[v] = true;
  }
  CHECK((saw[1] && saw[2] && saw[3]));

  const AnnType always[] = {
      AnnType::ArmDoors,          AnnType::BoardingComplete,
      AnnType::DisarmDoors,       AnnType::AfterTakeoff,
      AnnType::PreSafetyBriefing, AnnType::SafetyBriefing,
      AnnType::DescentStarted,    AnnType::DeboardingStarted,
      AnnType::SeatbeltSignOn,    AnnType::BoardingWelcome,
      AnnType::AfterLanding};
  // night has no baw files d-28 covers it
  const TimeOfDay day[] = {TimeOfDay::Morning, TimeOfDay::Afternoon,
                           TimeOfDay::Evening, TimeOfDay::Night};
  // 0 stays covered in case a stale variant survives
  for (int v = 0; v <= 3; ++v)
    for (Family fam : {Family::A319, Family::A320, Family::A321})
      for (bool neo : {false, true})
        for (TimeOfDay t : day)
          for (AnnType type : always) {
            if (fam == Family::A319 && neo) continue;
            SelectContext c = ctxFor(fam, neo, t);
            c.variant = v;
            for (unsigned seed = 1; seed <= 3; ++seed) {
              Rng r(seed);
              const Selection s = selectSound(baw, type, c, r);
              INFO("variant ", v, " type ", annTypeName(type), " time ",
                   timeOfDayName(t), " aircraft ", c.aircraft.label);
              REQUIRE(s.file);
              if (!s.file->variants.empty())
                CHECK(s.file->hasVariant(s.variant));
            }
          }

  SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Morning);
  c.variant = 2;  // the session variant is used where it exists
  CHECK(pick(baw, AnnType::ArmDoors, c) == "ArmDoors[2].ogg");
  CHECK(pick(baw, AnnType::AfterTakeoff, c) == "AfterTakeoff[Morning][2].ogg");
  CHECK(pick(baw, AnnType::PreSafetyBriefing, c) ==
        "PreSafetyBriefing[2][A320].ogg");
  CHECK(pick(baw, AnnType::SafetyBriefing, c) == "SafetyBriefing.ogg");
  c.variant = 3;  // no [3] and no untagged file: [1] or [2]
  Rng r(5);
  const Selection s = selectSound(baw, AnnType::AfterTakeoff, c, r);
  REQUIRE(s.file);
  CHECK(s.variantFallback);
  CHECK((s.variant == 1 || s.variant == 2));
  CHECK(pick(baw, AnnType::ArmDoors, c) == "ArmDoors[3].ogg");
  // d-28 night: nearest time of day, the session variant kept
  c.variant = 2;
  c.time = TimeOfDay::Night;
  c.localHour = 1.0;
  CHECK(pick(baw, AnnType::AfterTakeoff, c) == "AfterTakeoff[Evening][2].ogg");
  CHECK(pick(baw, AnnType::BoardingWelcome, c) ==
        "BoardingWelcome[Evening].ogg");
  CHECK(pick(baw, AnnType::AfterLanding, c) == "AfterLanding[Evening].ogg");
  c.localHour = 5.5;
  CHECK(pick(baw, AnnType::AfterTakeoff, c) == "AfterTakeoff[Morning][2].ogg");
  CHECK(pick(baw, AnnType::AfterLanding, c) == "AfterLanding[Morning].ogg");
  {
    Rng r2(1);
    const Selection n = selectSound(baw, AnnType::AfterLanding, c, r2);
    CHECK(n.timeFallback);
    CHECK(n.time == TimeOfDay::Morning);
    CHECK(n.variant == 0);  // untagged the normal fenix fallback
  }
  // a refuelling file fits at night so it plays, no fallback
  c.refueling = true;
  CHECK(pick(baw, AnnType::BoardingWelcome, c) ==
        "BoardingWelcome[Refueling].ogg");
}

TEST_CASE("time of day: nearest when no file fits (D-28)") {
  using T = TimeOfDay;
  CHECK(nearestTimesOfDay(T::Night, 1.0) ==
        std::vector<T>{T::Evening, T::Morning, T::Afternoon});
  CHECK(nearestTimesOfDay(T::Night, 5.0) ==
        std::vector<T>{T::Morning, T::Evening, T::Afternoon});
  CHECK(nearestTimesOfDay(T::Night, 0.0) ==
        std::vector<T>{T::Evening, T::Morning, T::Afternoon});
  // 09:00 is 3 h from both: the coming afternoon first
  CHECK(nearestTimesOfDay(T::Morning, 9.0) ==
        std::vector<T>{T::Afternoon, T::Night, T::Evening});
  CHECK(nearestTimesOfDay(T::Evening, 18.5) ==
        std::vector<T>{T::Afternoon, T::Night, T::Morning});
  // no clock the middle of the period
  CHECK(nearestTimesOfDay(T::Night, -1) ==
        std::vector<T>{T::Morning, T::Evening, T::Afternoon});
  CHECK(nearestTimesOfDay(T::Afternoon, 37.0) ==
        std::vector<T>{T::Morning, T::Evening, T::Night});  // 13:00

  SelectContext c = ctxFor(Family::A320, false, T::Morning);
  c.localHour = 9.0;
  // eju style afternoon only
  const Pack eju = makePack("EJU", {"AfterLanding[Afternoon].ogg"});
  CHECK(pick(eju, AnnType::AfterLanding, c) == "AfterLanding[Afternoon].ogg");
  // an untagged file that fits beats another time of day
  const Pack mix =
      makePack("MIX", {"BoardingWelcome.ogg", "BoardingWelcome[Evening].ogg"});
  c.time = T::Night;
  c.localHour = 2.0;
  CHECK(pick(mix, AnnType::BoardingWelcome, c) == "BoardingWelcome.ogg");
  // only time is relaxed: aircraft still has to match
  const Pack a319 = makePack("ACF", {"SafetyBriefing[Morning][A319].ogg"});
  CHECK(pick(a319, AnnType::SafetyBriefing, c) == "(none)");
  // untimed files that fail other tags never get a time fallback
  const Pack refuel = makePack("REF", {"BoardingWelcome[Refueling].ogg"});
  CHECK(pick(refuel, AnnType::BoardingWelcome, c) == "(none)");
  // the explainer candidates come from the pass that played
  Rng r(1);
  const Selection s = selectSound(eju, AnnType::AfterLanding,
                                  ctxFor(Family::A320, false, T::Night), r);
  REQUIRE(s.file);
  CHECK(s.timeFallback);
  CHECK(s.time == T::Afternoon);
  REQUIRE(s.candidates.size() == 1);
  CHECK(s.candidates[0].eligible);
}

TEST_CASE("variants: choices, gaps, custom types, explicit [0]") {
  CHECK(
      makePack("NOV", {"ArmDoors.ogg", "AfterLanding.ogg"}).variantChoices() ==
      std::vector<int>{0});
  // gap at 2 is never picked untagged armdoors competes with [1]
  const Pack gap = makePack(
      "GAP", {"ArmDoors.ogg", "ArmDoors[1].ogg", "AfterLanding[3].ogg"});
  CHECK(gap.variantChoices() == std::vector<int>{0, 1, 3});
  Rng rng(2);
  for (int k = 0; k < 100; ++k) CHECK(pickVariant(gap, rng) != 2);
  // untagged files shared by all variants dont make 0 a choice
  CHECK(makePack("SHR",
                 {"ArmDoors[1].ogg", "ArmDoors[2].ogg", "AfterLanding.ogg"})
            .variantChoices() == std::vector<int>{1, 2});
  // one variant only like uales
  const Pack one = makePack("ONE", {"AfterTakeoff[2].ogg", "ArmDoors[2].ogg"});
  CHECK(one.variantChoices() == std::vector<int>{2});
  for (int k = 0; k < 20; ++k) CHECK(pickVariant(one, rng) == 2);
  // custom types are never played so their variants dont count
  CHECK(
      makePack("CUS", {"ArmDoors.ogg", "Turbulence[5].ogg"}).variantChoices() ==
      std::vector<int>{0});
  // explicit [0] is variant 0
  const Pack zero = makePack("ZER", {"ArmDoors[0].ogg", "ArmDoors[1].ogg"});
  CHECK(zero.variantChoices() == std::vector<int>{0, 1});
  SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Morning);
  c.variant = 0;
  CHECK(pick(zero, AnnType::ArmDoors, c) == "ArmDoors[0].ogg");
  c.variant = 7;  // stale variant falls back to 0 first fenix
  CHECK(pick(zero, AnnType::ArmDoors, c) == "ArmDoors[0].ogg");
}

TEST_CASE("selection: ties avoid the file played last") {
  const Pack p =
      makePack("XXX", {"BoardingWelcome[1].ogg", "BoardingWelcome~a.ogg",
                       "BoardingWelcome~b.ogg"});
  SelectContext c = ctxFor(Family::A321, false, TimeOfDay::Morning);
  Rng rng(3);
  std::string last;
  for (int k = 0; k < 30; ++k) {
    const Selection s = selectSound(p, AnnType::BoardingWelcome, c, rng, last);
    REQUIRE(s.file);
    CHECK(s.file->path != last);
    CHECK(s.file->relPath !=
          "BoardingWelcome[1].ogg");  // 1 isnt the session variant
    last = s.file->path;
    CHECK(s.candidates.size() == 3);
  }
}

TEST_CASE("airline table: registrations, names, IATA") {
  CHECK(findRegistrations("[LEA] EasyJet (G-UZHH)") ==
        std::vector<std::string>{"G-UZHH"});
  CHECK(findRegistrations("Delta Air Lines N427DX 8K") ==
        std::vector<std::string>{"N427DX"});
  CHECK(findRegistrations("Lufthansa (D-AIJB) 8K") ==
        std::vector<std::string>{"D-AIJB"});
  CHECK(findRegistrations("Air Canada (CFM - fences - C-FGKN) 8K") ==
        std::vector<std::string>{"C-FGKN"});
  CHECK(normalizeWords("[LEA] EasyJet (G-UZHH)") == "lea easyjet g uzhh");

  const AirlineTable t = AirlineTable::builtin();
  CHECK(t.size() > 100);
  CHECK(t.icaoForIata("LH") == std::vector<std::string>{"DLH"});
  CHECK(t.icaoForLiveryName("[LEA] EasyJet (G-UZHH)").front() == "EZY");
  CHECK(t.icaoForLiveryName("easyJet Europe OE-IJA").front() == "EJU");
  CHECK(t.icaoForLiveryName("easyJet HB-JXA").front() == "EZS");
  CHECK(
      t.icaoForLiveryName("British Airways (G-EUXG) crest livery 8K").front() ==
      "BAW");
  CHECK(t.icaoForLiveryName("Air India VT-EXS (8K)").front() == "AIC");
  CHECK(t.icaoForLiveryName("Airbus House F-WTTE (Airbus Colors)").empty());
  CHECK(t.nameFor("DLH") == "Lufthansa");  // flight card names
  CHECK(t.nameFor("baw") == "British Airways");
  CHECK(t.nameFor("ZZZ").empty());

  AirlineTable user = AirlineTable::parse("XYZ\tLH\tLufthansa\n");
  AirlineTable all = AirlineTable::builtin();
  all.prepend(user);
  CHECK(all.icaoForIata("LH").front() == "XYZ");
}

TEST_CASE("pack resolution order (VP_SPEC § 1.4)") {
  PackLibrary lib;
  for (const char* n :
       {"AAL", "BAW", "DAL", "DLH", "EZY", "JBU", "LCA", "Default"})
    lib.packs.push_back(makePack(n, {"ArmDoors.ogg"}));
  Pack ales = makePack("UALES", {"ArmDoors.ogg"});
  ales.mappingOnly = true;
  lib.packs.push_back(ales);
  const AirlineTable t = AirlineTable::builtin();
  auto res = [&](const std::string& livery, const std::string& fn = "",
                 const LiveryConfig& cfg = {}, const std::string& ofp = "") {
    const Resolution r =
        resolvePack(lib, t, ResolveInput{livery, fn, cfg, ofp});
    return r.pack ? r.pack->name : std::string("(none)");
  };
  CHECK(res("British Airways (G-EUXG) crest livery 8K") == "BAW");
  CHECK(res("Lufthansa (D-AIJB) 8K") == "DLH");
  CHECK(res("[LEA] EasyJet (G-UZHH)") == "EZY");
  CHECK(res("Airbus House F-WTTE (Airbus Colors)") == "Default");
  CHECK(res("Delta Air Lines N427DX 8K") == "DAL");
  CHECK(res("JetBlue Balloons N957JB Knock Knock Blue's There") == "JBU");
  CHECK(res("(IAE) American 'US Airways Heritage' (N578UW)") ==
        "AAL");                                     // awe not installed next
  CHECK(res("Cathay Pacific B-LCA") == "Default");  // lca is the reg not a code
  CHECK(res("DLH - Lufthansa D-AIUA") == "DLH");
  CHECK(res("") == "Default");

  // flight number beats the livery name iata thru the table
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "DLH400") == "DLH");
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "LH400") == "DLH");
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "XYZ123") == "BAW");

  // ofp callsign beats mcdu livery name but not the mappings
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "BAW12", {},
            "DLH400") == "DLH");
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "", {}, "EZY12") ==
        "EZY");
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "DLH400", {}, "XYZ1") ==
        "DLH");
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "", {}, "") ==
        "BAW");  // failed check empty
  {
    LiveryConfig m;
    m.airline = "JBU";
    m.source = "liveries.ini [x]";
    CHECK(res("British Airways (G-EUXG) crest livery 8K", "", m, "DLH400") ==
          "JBU");
    const Resolution r =
        resolvePack(lib, t, ResolveInput{"x", "", {}, "DLH400"});
    CHECK(r.reason == "SimBrief OFP DLH400");
  }

  // mapping beats everything mapping only packs only reachable this way
  LiveryConfig cfg;
  cfg.airline = "UALES";
  cfg.source = "airlinepack.cfg";
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "DLH400", cfg) ==
        "UALES");
  CHECK(res("Some UALES livery") == "Default");
  cfg.airline = "QQQ";
  CHECK(res("British Airways (G-EUXG) crest livery 8K", "", cfg) == "BAW");

  const Ini central = Ini::parse(
      "[Airbus House*]\nairline = DLH\ncustom_tags = WiFi\n[*]\noverwater = "
      "yes\n");
  const LiveryConfig c1 =
      liveryConfigFromCentral(central, "Airbus House F-WTTE (Airbus Colors)");
  CHECK(c1.airline == "DLH");
  CHECK(c1.customTags == std::vector<std::string>{"WiFi"});
  CHECK(c1.source == "liveries.ini [Airbus House*]");
  CHECK(res("Airbus House F-WTTE (Airbus Colors)", "", c1) == "DLH");
  const LiveryConfig c2 = liveryConfigFromCentral(central, "Lufthansa");
  CHECK(c2.overwater == true);

  const Ini side = Ini::parse(
      "icao_airline = BAW\nfnx_airline_pack_custom_tags = USB, WiFi\noverwater "
      "= auto\n");
  const LiveryConfig s = liveryConfigFromSidecar(side);
  CHECK(s.airline == "BAW");
  CHECK(s.customTags.size() == 2);
  CHECK(!s.overwater);
  const LiveryConfig m = mergeLiveryConfig(s, c1);
  CHECK(m.airline == "BAW");
  CHECK(m.source == "airlinepack.cfg");
}

TEST_CASE("sample packs in ../Packs (local only)") {
  std::error_code ec;
  if (!std::filesystem::is_directory(packsDir(), ec)) {
    MESSAGE("../Packs not present: skipped");
    return;
  }
  const PackLibrary lib = scanPacks({packsDir()});
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
  CHECK(std::any_of(w.begin(), w.end(), [](const std::string& s) {
    return s.find(".mp4") != std::string::npos;
  }));
  // every sample file has a known type
  for (const Pack& p : lib.packs)
    for (const SoundFile& f : p.files)
      CHECK_MESSAGE(f.type.has_value(), p.name, "/", f.relPath);

  SelectContext c = ctxFor(Family::A320, false, TimeOfDay::Evening);
  c.refueling = true;
  CHECK(pick(*lib.find("DLH"), AnnType::BoardingWelcome, c) ==
        "BoardingWelcome[Refueling][Evening].ogg");
  c = ctxFor(Family::A321, false, TimeOfDay::Afternoon);
  CHECK(pick(*lib.find("EJU"), AnnType::AfterLanding, c) ==
        "AfterLanding[Afternoon].ogg");
  CHECK(pick(*lib.find("EJU"), AnnType::DescentStarted, c) ==
        "DescentSeatbelt.ogg");
  CHECK(pick(*lib.find("UAL"), AnnType::SafetyBriefing,
             ctxFor(Family::A321, true, TimeOfDay::Morning)) ==
        "SafetyBriefing[A21N].ogg");
  CHECK(pick(*lib.find("UAL"), AnnType::SafetyBriefing,
             ctxFor(Family::A321, false, TimeOfDay::Morning)) == "(none)");
  c = ctxFor(Family::A320, false, TimeOfDay::Morning);
  c.variant = 1;
  CHECK(pick(*lib.find("UAL"), AnnType::BoardingComplete, c) ==
        "BoardingComplete[1].ogg");
  c.context = CallContext::Landing;
  CHECK(pick(*lib.find("UAL"), AnnType::CrewTakeSeats, c) ==
        "CrewSeatsLanding.ogg");  // 1 is the skipped mp4
}
