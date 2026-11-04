// purser tests whole flights thru the real state machine w a fake player
#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>

#include "core/purser.h"
#include "core/recorder.h"
#include "core/toliss_decode.h"

using namespace fxa;

namespace {

struct FakeOut : PurserOutput {
  const double* clock = nullptr;
  std::set<AnnType> missing;            // types the fake pack lacks
  std::map<AnnType, double> durations;  // default 8s
  std::vector<std::string> played;      // type or type takeoff
  std::vector<std::pair<double, bool>> music;
  int chimes = 0;
  double endsAt = -1;
  int stops = 0;

  std::optional<std::string> play(AnnType t, CallContext c,
                                  bool interphone) override {
    if (missing.count(t)) return std::nullopt;
    std::string label = annTypeName(t);
    if (c != CallContext::Any) label += std::string("(") + contextName(c) + ")";
    if (interphone) label += "#ip";
    played.push_back(label);
    const auto d = durations.find(t);
    endsAt = *clock + (d != durations.end() ? d->second : 8.0);
    return label;
  }
  bool playing() const override { return *clock < endsAt; }
  void stop() override {
    endsAt = -1;
    ++stops;
  }
  void chime() override { ++chimes; }
  void setMusic(bool on) override { music.emplace_back(*clock, on); }
  std::optional<Mode> peek(AnnType t, CallContext) const override {
    if (missing.count(t)) return std::nullopt;
    return t == AnnType::CabinSecured ? Mode::Interphone : Mode::Manual;
  }
};

struct Flight {
  FakeOut out;
  Settings set;
  SimState s;
  Purser purser{out, 42};
  double t = 0;
  bool settled = false;

  Flight() {
    out.clock = &t;
    out.durations[AnnType::SafetyBriefing] = 150;
    out.durations[AnnType::BoardingMusic] = 0;
    set.chainGap = 2;
    set.boardingStart =
        static_cast<int>(BoardingStart::Doors);  // these board when doors open
    s.onGround = true;
    s.acPower = true;
    s.pax = 150;
    s.slidesKnown = true;
    s.sunPitchDeg = 30;
    s.fmgsPhase = Phase::PreFlight;
    s.fmgsPhaseRaw = 0;
    purser.setAircraft(makeAircraftType(Family::A321, true));
  }
  // hold state for seconds at 4hz
  void run(double seconds, const std::function<void(SimState&)>& change = {}) {
    if (change) change(s);
    if (!settled) {
      // start state is taken 10s after activation 4.4 flights below start after
      // that
      settled = true;
      seconds += 10.25;
    }
    for (double e = 0; e < seconds; e += 0.25) {
      t += 0.25;
      s.t = t;
      if (s.zuluSec >= 0) s.zuluSec = secondOfDay(s.zuluSec + 0.25);
      purser.tick(s, set);
    }
  }
  // true if pred holds before maxseconds
  bool runUntil(const std::function<bool()>& pred, double maxSeconds) {
    for (double e = 0; e < maxSeconds; e += 0.25) {
      if (pred()) return true;
      run(0.25);
    }
    return pred();
  }
  void phase(Phase p) { s.fmgsPhase = p, s.fmgsPhaseRaw = static_cast<int>(p); }
  int count(const std::string& label) const {
    return static_cast<int>(
        std::count(out.played.begin(), out.played.end(), label));
  }
  size_t indexOf(const std::string& label) const {
    return static_cast<size_t>(
        std::find(out.played.begin(), out.played.end(), label) -
        out.played.begin());
  }
  Outcome lastOutcome(AnnType type, CallContext c = CallContext::Any) const {
    const auto& tl = purser.timeline();
    for (auto r = tl.rbegin(); r != tl.rend(); ++r)
      if (r->type == type && r->context == c) return r->outcome;
    return Outcome::Queued;
  }

  // flight segments
  void boardAndDepart() {
    run(30, [](SimState& x) {
      x.doorMax = 1, x.doorFwd = 1;
    });        // doors open at the gate
    run(600);  // welcomes every 2-6 min
    run(20,
        [](SimState& x) { x.doorMax = 0, x.doorFwd = 0; });  // auto doors close
    run(10, [](SimState& x) { x.beacon = true, x.seatbelt = true; });
    run(60, [](SimState& x) { x.gsKt = 2; });  // push
    run(60, [](SimState& x) { x.enginesRunning = 2, x.gsKt = 0; });
    s.gsKt = 15;  // taxi till the cabin calls
    runUntil([&] { return purser.calling(); }, 600);
  }
  // pull cab to answer cabinsecured then push it back
  void answerCab() {
    run(5, [](SimState& x) { x.cabOn = true; });
    run(10, [](SimState& x) { x.cabOn = false; });
  }
  // lineup signal d23
  void lineUp() {
    run(20, [](SimState& x) { x.landingLights = true; });
  }
  void takeoffAndClimb() {
    phase(Phase::Takeoff);
    run(30, [](SimState& x) { x.gsKt = 140; });
    run(20,
        [](SimState& x) { x.onGround = false, x.aglFt = 800, x.vsFpm = 2500; });
    phase(Phase::Climb);
    run(200, [](SimState& x) { x.aglFt = 8000; });
    run(30, [](SimState& x) {
      x.seatbelt = false, x.aglFt = 12000;
    });  // signs off in the climb
    phase(Phase::Cruise);
    run(900,
        [](SimState& x) { x.aglFt = 36000, x.mslFt = 36000, x.vsFpm = 0; });
  }
  void descendAndLand() {
    phase(Phase::Descent);
    run(300, [](SimState& x) {
      x.vsFpm = -2000, x.aglFt = 20000, x.mslFt = 20000, x.seatbelt = true;
    });
    run(400, [](SimState& x) { x.aglFt = 9000, x.mslFt = 9000; });
    phase(Phase::Approach);
    run(300, [](SimState& x) {
      x.aglFt = 3000, x.mslFt = 3000, x.gearLeverDown = 1;
    });  // gear down seats d24
    run(30, [](SimState& x) { x.aglFt = 500; });
    phase(Phase::Done);
    run(20, [](SimState& x) {
      x.onGround = true, x.aglFt = 0, x.gsKt = 120, x.speedbrake = 1,
      x.landingLights = true;
    });
    run(150, [](SimState& x) {
      x.gsKt = 15, x.speedbrake = 0, x.landingLights = false;
    });  // taxi in
    run(30, [](SimState& x) {
      x.gsKt = 0, x.parkBrake = true, x.enginesRunning = 0, x.beacon = false;
    });
    run(400, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  }
};

}  // namespace

TEST_CASE("a normal flight plays every call in order") {
  Flight f;
  f.run(5);
  f.boardAndDepart();
  CHECK(f.count("BoardingWelcome") >= 2);
  CHECK(f.count("BoardingComplete") == 1);
  CHECK(f.count("ArmDoors") == 1);
  CHECK(f.count("PreSafetyBriefing") == 1);
  CHECK(f.count("SafetyBriefing") == 1);
  CHECK(f.count("CrewTakeSeats(takeoff)") == 0);  // d23 waits for lineup
  CHECK(f.out.chimes >= 1);
  REQUIRE(f.purser.calling());  // waiting for cab
  f.run(5, [](SimState& x) { x.cabOn = true; });
  CHECK(f.count("CabinSecured(takeoff)#ip") == 1);
  f.run(10, [](SimState& x) { x.cabOn = false; });
  CHECK(f.purser.packLocked());
  CHECK(f.purser.stageLabel() == "Cabin secured, waiting for line-up");
  f.lineUp();
  CHECK(f.count("CrewTakeSeats(takeoff)") == 1);
  CHECK(f.count("CabinDim(takeoff)") == 0);  // daylight

  f.takeoffAndClimb();
  CHECK(f.count("AfterTakeoff") == 1);
  CHECK(f.count("SeatbeltSignOff") == 1);
  CHECK(f.count("CabinService") == 1);
  CHECK(f.indexOf("AfterTakeoff") < f.indexOf("SeatbeltSignOff"));

  f.descendAndLand();
  CHECK(f.count("DescentStarted") == 1);
  CHECK(f.count("SeatbeltSignOn") == 0);  // descent announcement covers it
  CHECK(f.count("CrewTakeSeats(landing)") == 1);
  CHECK(f.lastOutcome(AnnType::CabinSecured, CallContext::Landing) ==
        Outcome::Unanswered);  // nobody pulled cab
  CHECK(f.count("AfterLanding") == 1);
  CHECK(f.count("DisarmDoors") == 1);
  CHECK(f.count("DeboardingStarted") == 1);
  CHECK(f.count("DeboardingComplete") == 1);

  // music only in boarding/deboarding
  REQUIRE(f.out.music.size() >= 4);
  CHECK(f.out.music[0].second == true);
  CHECK(f.out.music[1].second == false);
  CHECK(f.out.music[2].second == true);
  CHECK(f.out.music[3].second == false);

  const std::vector<std::string> order = {"BoardingComplete",
                                          "ArmDoors",
                                          "PreSafetyBriefing",
                                          "SafetyBriefing",
                                          "CabinSecured(takeoff)#ip",
                                          "CrewTakeSeats(takeoff)",
                                          "AfterTakeoff",
                                          "CabinService",
                                          "DescentStarted",
                                          "CrewTakeSeats(landing)",
                                          "AfterLanding",
                                          "DisarmDoors",
                                          "DeboardingStarted",
                                          "DeboardingComplete"};
  for (size_t k = 1; k < order.size(); ++k)
    CHECK_MESSAGE(f.indexOf(order[k - 1]) < f.indexOf(order[k]), order[k]);
}

TEST_CASE(
    "unanswered cabin call: chime plus 3 retries, then the cabin gives up") {
  Flight f;
  f.run(5);
  f.boardAndDepart();
  REQUIRE(f.purser.calling());
  const int before = f.out.chimes;
  f.run(200);
  CHECK(f.out.chimes - before == 3);
  CHECK(f.lastOutcome(AnnType::CabinSecured, CallContext::Takeoff) ==
        Outcome::Unanswered);
  CHECK(f.count("CabinSecured(takeoff)#ip") == 0);
}

TEST_CASE(
    "takeoff thrust stops the safety briefing and drops the pre-takeoff "
    "calls") {
  Flight f;
  f.run(5);
  f.run(30, [](SimState& x) { x.doorMax = 1; });
  f.run(60);
  f.run(20, [](SimState& x) {
    x.doorMax = 0, x.beacon = true, x.enginesRunning = 2, x.gsKt = 2;
  });  // push
  REQUIRE(
      f.runUntil([&] { return f.count("SafetyBriefing") == 1; }, 200));  // d22
  f.phase(Phase::Takeoff);
  f.run(10, [](SimState& x) { x.gsKt = 60; });
  CHECK(f.out.stops >= 1);
  CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Stopped);
  f.run(20, [](SimState& x) { x.onGround = false, x.aglFt = 1000; });
  f.phase(Phase::Climb);
  f.run(100, [](SimState& x) { x.aglFt = 5000; });
  CHECK(f.count("CrewTakeSeats(takeoff)") == 0);
  CHECK(f.count("AfterTakeoff") == 1);
}

TEST_CASE("missing sounds are skipped and the chain continues") {
  Flight f;
  f.out.missing = {AnnType::PreSafetyBriefing, AnnType::BoardingWelcome,
                   AnnType::CabinSecured};
  f.run(5);
  f.boardAndDepart();
  f.lineUp();
  CHECK(f.count("SafetyBriefing") == 1);
  CHECK(f.count("CrewTakeSeats(takeoff)") == 1);
  CHECK(f.out.chimes == 0);  // no cabinsecured sound no call fenix
  CHECK(f.lastOutcome(AnnType::PreSafetyBriefing) == Outcome::NoSound);
}

TEST_CASE("disabled events are skipped") {
  Flight f;
  f.set.disabledEvents = {"SafetyBriefing"};
  f.run(5);
  f.boardAndDepart();
  f.answerCab();
  f.lineUp();
  CHECK(f.count("SafetyBriefing") == 0);
  CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Disabled);
  CHECK(f.count("CrewTakeSeats(takeoff)") == 1);
}

TEST_CASE("darkness adds the cabin dim calls") {
  Flight f;
  f.s.sunPitchDeg = -10;
  f.run(5);
  f.boardAndDepart();
  f.answerCab();
  f.lineUp();
  CHECK(f.count("CabinDim(takeoff)") == 1);
  CHECK(f.indexOf("CrewTakeSeats(takeoff)") < f.indexOf("CabinDim(takeoff)"));
}

TEST_CASE("decompression pre-empts everything and cancels cabin service") {
  Flight f;
  f.run(5);
  f.boardAndDepart();
  f.phase(Phase::Takeoff);
  f.run(10, [](SimState& x) { x.gsKt = 140; });
  f.run(10, [](SimState& x) { x.onGround = false, x.aglFt = 1000; });
  f.phase(Phase::Climb);
  f.run(100, [](SimState& x) { x.aglFt = 20000; });
  f.phase(Phase::Cruise);
  f.run(60, [](SimState& x) { x.aglFt = 35000, x.mslFt = 35000; });
  f.run(30, [](SimState& x) { x.paxMasks = true; });
  CHECK(f.count("Decompression") == 1);
  f.run(900);
  CHECK(f.count("CabinService") == 0);
}

TEST_CASE(
    "starting airborne in cruise: no departure calls, the descent still "
    "comes") {
  Flight f;
  f.s.onGround = false;
  f.s.aglFt = 35000;
  f.s.mslFt = 35000;
  f.phase(Phase::Cruise);
  f.run(60);
  CHECK(f.out.played.empty());
  f.phase(Phase::Descent);
  f.run(200, [](SimState& x) { x.vsFpm = -1500, x.aglFt = 25000; });
  CHECK(f.count("DescentStarted") == 1);
  CHECK(f.count("ArmDoors") == 0);
  CHECK(f.count("AfterTakeoff") == 0);
}

TEST_CASE(
    "starting on the runway with engines running skips boarding and the "
    "briefing") {
  Flight f;
  f.s.enginesRunning = 2;
  f.s.gsKt = 5;
  f.s.beacon = true;
  f.run(120);
  CHECK(f.out.played.empty());
}

TEST_CASE("go-around drops pending landing calls without repeating them") {
  Flight f;
  f.s.onGround = false;
  f.s.aglFt = 12000;
  f.s.mslFt = 12000;
  f.phase(Phase::Cruise);
  f.run(10);
  f.phase(Phase::Descent);
  f.run(150, [](SimState& x) {
    x.vsFpm = -1000, x.aglFt = 8000;
  });  // descentstarted then the landing check
  REQUIRE(f.count("DescentStarted") == 1);
  f.phase(Phase::Approach);
  f.run(2, [](SimState& x) { x.aglFt = 2000; });
  f.phase(Phase::GA);
  f.run(60, [](SimState& x) { x.aglFt = 1500, x.vsFpm = 2000; });
  f.phase(Phase::Approach);
  f.run(400, [](SimState& x) { x.aglFt = 2500, x.vsFpm = -800; });
  CHECK(f.count("CrewTakeSeats(landing)") <= 1);
}

TEST_CASE("advance: boarding straight to BoardingComplete, then timers") {
  Flight f;
  f.run(5);
  f.run(30, [](SimState& x) { x.doorMax = 1; });
  f.run(60);
  REQUIRE(f.purser.stageLabel() == "Boarding");
  f.purser.advance();
  f.run(15);
  CHECK(f.count("BoardingComplete") == 1);
  f.run(20, [](SimState& x) { x.doorMax = 0, x.beacon = true; });
  REQUIRE(f.count("ArmDoors") == 1);
  f.purser.advance();  // presafety delay
  f.run(5);
  CHECK(f.count("PreSafetyBriefing") == 1);
  f.purser.skip();  // stop it safetybriefing follows
  f.run(5);
  CHECK(f.lastOutcome(AnnType::PreSafetyBriefing) == Outcome::Stopped);
  CHECK(f.count("SafetyBriefing") == 1);
}

TEST_CASE("no AC power: nothing plays until power is on") {
  Flight f;
  f.s.acPower = false;
  f.run(5);
  f.run(200, [](SimState& x) { x.doorMax = 1; });
  CHECK(f.out.played.empty());
  CHECK(f.out.music.empty());
  f.run(200, [](SimState& x) { x.acPower = true; });
  CHECK(f.count("BoardingWelcome") >= 1);
}

TEST_CASE("turnaround: a second boarding after the gap") {
  Flight f;
  f.set.turnaround = true;
  f.set.turnaroundGap = 120;
  f.run(5);
  f.boardAndDepart();
  f.run(5, [](SimState& x) { x.cabOn = true; });
  f.run(5, [](SimState& x) { x.cabOn = false; });
  f.takeoffAndClimb();
  f.descendAndLand();
  REQUIRE(f.count("DeboardingComplete") == 1);
  CHECK(!f.purser.packLocked());
  const int welcomes = f.count("BoardingWelcome");
  f.run(400);  // doors stay open next boarding after the gap
  CHECK(f.count("BoardingWelcome") > welcomes);
}

// cabin call buttons 4.3a

TEST_CASE(
    "call mode: open doors alone don't start boarding; a cabin call does") {
  Flight f;
  f.set.boardingStart = static_cast<int>(BoardingStart::Call);
  f.run(5);
  f.run(180, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  CHECK(f.out.played.empty());
  CHECK(f.out.music.empty());
  CHECK(f.purser.nextLabel().find("cabin call") != std::string::npos);
  f.purser.cabinCall();
  f.run(60);
  CHECK(f.count("BoardingWelcome") == 1);
  REQUIRE(!f.out.music.empty());
  CHECK(f.out.music.back().second);
  f.run(20, [](SimState& x) { x.doorMax = 0, x.doorFwd = 0; });
  CHECK(f.count("BoardingComplete") ==
        0);  // d1 closing doors dont end boarding
  CHECK(f.purser.stageLabel() == "Boarding");
  CHECK(f.runUntil([&] { return f.count("BoardingComplete") == 1; },
                   1800));  // modeled time
}

TEST_CASE("call mode: a call with the doors shut boards until the beacon") {
  Flight f;
  f.set.boardingStart = static_cast<int>(BoardingStart::Call);
  f.run(5);
  f.purser.cabinCall();
  f.run(300);
  CHECK(f.count("BoardingWelcome") >= 1);
  CHECK(f.count("BoardingComplete") ==
        0);  // doors never opened so them being shut doesnt end it either
  f.run(20, [](SimState& x) { x.beacon = true; });
  CHECK(f.count("BoardingComplete") == 1);
  CHECK(f.count("ArmDoors") == 1);
}

TEST_CASE("call mode: no AC power, the call is ignored") {
  Flight f;
  f.set.boardingStart = static_cast<int>(BoardingStart::Call);
  f.s.acPower = false;
  f.run(5);
  f.purser.cabinCall();
  f.run(1);  // handled this tick still no power
  f.run(120, [](SimState& x) { x.acPower = true; });
  CHECK(f.out.played.empty());
  CHECK(f.purser.stageLabel() == "At the gate");
}

TEST_CASE("call mode: Advance starts boarding") {
  Flight f;
  f.set.boardingStart = static_cast<int>(BoardingStart::Call);
  f.run(5);
  f.purser.advance();
  f.run(60);
  CHECK(f.purser.stageLabel() == "Boarding");
}

TEST_CASE(
    "a cabin call during the safety briefing makes the cabin check quick "
    "(Fenix expedite)") {
  Flight f;
  f.set.expediteSeconds = 10;
  f.run(5);
  f.run(20,
        [](SimState& x) { x.beacon = true, x.enginesRunning = 2, x.gsKt = 2; });
  REQUIRE(f.runUntil([&] { return f.count("SafetyBriefing") == 1; }, 200));
  const double briefingStart = f.t;
  f.purser.cabinCall();  // not taxied hurries the check not lineup
  f.run(1);
  // briefing is 150s in the fake player expedited check at most 10s then
  // cabinsecured
  REQUIRE(f.runUntil([&] { return f.purser.calling(); }, 400));
  CHECK(f.t - briefingStart < 150 + 10 + 4);
  CHECK(f.count("CrewTakeSeats(takeoff)") == 0);
  CHECK(f.lastOutcome(AnnType::SafetyBriefing) ==
        Outcome::Played);  // briefing not cut
}

TEST_CASE("a cabin call during the cabin check before takeoff expedites it") {
  Flight f;
  f.out.durations[AnnType::SafetyBriefing] = 20;
  f.run(5);
  f.run(20,
        [](SimState& x) { x.beacon = true, x.enginesRunning = 2, x.gsKt = 2; });
  REQUIRE(f.runUntil(
      [&] { return f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Played; },
      300));
  f.run(5);  // 2 min check for 150 pax on a321
  REQUIRE(!f.purser.calling());
  f.purser.cabinCall();
  CHECK(f.runUntil([&] { return f.purser.calling(); }, 20));
}

TEST_CASE(
    "a cabin call in the descent expedites the cabin check before landing") {
  Flight f;
  f.s.onGround = false;
  f.s.aglFt = 30000;
  f.s.mslFt = 30000;
  f.phase(Phase::Cruise);
  f.run(10);
  f.phase(Phase::Descent);
  f.run(5, [](SimState& x) { x.vsFpm = -2000; });
  f.purser.cabinCall();  // before descentstarted check after it is quick
  REQUIRE(f.runUntil(
      [&] { return f.lastOutcome(AnnType::DescentStarted) == Outcome::Played; },
      200));
  f.run(1, [](SimState& x) { x.aglFt = 9000; });  // under the ready height
  CHECK(f.runUntil([&] { return f.purser.calling(); }, 25));
}

TEST_CASE("a cabin call with nothing to do changes nothing") {
  Flight f;
  f.s.onGround = false;
  f.s.aglFt = 30000;
  f.phase(Phase::Cruise);
  f.run(10);
  f.purser.cabinCall();
  f.run(10);
  CHECK(f.out.played.empty());
}

// simbrief schedule 4.1a

namespace {

BoardingPlan planAt(double boardingAtSec, int seqno = 1) {
  BoardingPlan p;
  p.valid = true;
  p.seqno = seqno;
  p.boardingAtSec = boardingAtSec;
  p.etdSec = boardingAtSec + 35 * 60;
  p.summary = "test plan";
  return p;
}

void scheduleMode(Flight& f, double zulu) {
  f.set.boardingStart = static_cast<int>(BoardingStart::Schedule);
  f.s.zuluSec = zulu;
}

}  // namespace

TEST_CASE(
    "schedule: a plan seen before the boarding time waits silently, then "
    "boards on time") {
  Flight f;
  scheduleMode(f, 12 * 3600);
  f.purser.setBoardingPlan(planAt(12 * 3600 + 600));
  f.run(5);
  f.run(500, [](SimState& x) {
    x.doorMax = 1, x.doorFwd = 1;
  });  // auto door open way before boarding
  CHECK(f.out.played.empty());
  CHECK(f.out.music.empty());
  CHECK(f.purser.nextLabel().find("Boarding at 12:10Z") != std::string::npos);
  f.run(150);
  CHECK(f.purser.stageLabel() == "Boarding");
  CHECK(f.count("BoardingWelcome") >= 1);
  REQUIRE(!f.out.music.empty());
  CHECK(f.out.music.back().second);
  f.run(20, [](SimState& x) {
    x.doorMax = 0, x.doorFwd = 0;
  });  // auto closes it boarding goes on
  CHECK(f.count("BoardingComplete") == 0);
  // on schedule done 10 min before etd 12:10z board 12:45z etd no warning
  const double end = f.purser.boardingEndsAt();
  CHECK(end == doctest::Approx(600 + 25 * 60).epsilon(0.001));  // t 600 12:10z
  CHECK(f.purser.boardingNote().empty());
  REQUIRE(f.runUntil([&] { return f.count("BoardingComplete") == 1; }, 1800));
  CHECK(f.t == doctest::Approx(end).epsilon(0.002));
  CHECK(f.count("ArmDoors") == 0);  // fenix armdoors waits for the beacon
  f.run(20, [](SimState& x) { x.beacon = true; });
  CHECK(f.count("ArmDoors") == 1);
}

TEST_CASE(
    "schedule: the boarding time comes with the doors shut: boarding starts, "
    "ends on the beacon") {
  Flight f;
  scheduleMode(f, 12 * 3600);
  f.purser.setBoardingPlan(planAt(12 * 3600 + 60));
  f.run(120);
  CHECK(f.purser.stageLabel() == "Boarding");
  f.run(200);
  CHECK(f.count("BoardingComplete") == 0);
  f.run(10, [](SimState& x) { x.beacon = true; });
  CHECK(f.count("BoardingComplete") == 1);
}

TEST_CASE("schedule: a late load uses the doors rule") {
  Flight f;
  scheduleMode(f, 12 * 3600 + 300);  // boarding time was 5 min ago
  f.purser.setBoardingPlan(planAt(12 * 3600));
  f.run(60);
  CHECK(f.purser.stageLabel() == "At the gate");
  CHECK(f.purser.nextLabel().find("after a door is open") != std::string::npos);
  f.run(10, [](SimState& x) { x.doorMax = 1; });
  CHECK(f.purser.stageLabel() == "At the gate");  // not 15s yet
  f.run(10);
  CHECK(f.purser.stageLabel() == "Boarding");
}

TEST_CASE("schedule: without a valid plan the doors rule applies") {
  Flight f;
  scheduleMode(f, 12 * 3600);
  f.purser.setBoardingPlan(BoardingPlan{});
  f.run(30);
  CHECK(f.out.played.empty());
  f.run(20, [](SimState& x) { x.doorMax = 1; });
  CHECK(f.purser.stageLabel() == "Boarding");

  Flight g;  // plan but no zulu doors too
  g.set.boardingStart = static_cast<int>(BoardingStart::Schedule);
  g.purser.setBoardingPlan(planAt(12 * 3600));
  g.run(5);
  g.run(20, [](SimState& x) { x.doorMax = 1; });
  CHECK(g.purser.stageLabel() == "Boarding");
}

TEST_CASE("schedule: a cabin call before the boarding time starts boarding") {
  Flight f;
  scheduleMode(f, 12 * 3600);
  f.purser.setBoardingPlan(planAt(13 * 3600));
  f.run(30, [](SimState& x) { x.doorMax = 1; });
  CHECK(f.purser.stageLabel() == "At the gate");
  f.purser.cabinCall();
  f.run(60);
  CHECK(f.purser.stageLabel() == "Boarding");
  CHECK(f.count("BoardingWelcome") == 1);
}

TEST_CASE(
    "schedule: a plan re-timed into the past while waiting starts boarding "
    "now") {
  Flight f;
  scheduleMode(f, 12 * 3600);
  f.purser.setBoardingPlan(planAt(12 * 3600 + 1800));
  f.run(60, [](SimState& x) { x.doorMax = 1; });
  CHECK(f.purser.stageLabel() == "At the gate");
  f.purser.setBoardingPlan(planAt(12 * 3600 + 30, 2));  // new ofp w earlier etd
  f.run(5);
  CHECK(f.purser.stageLabel() == "Boarding");
}

// modeled boarding 4.1b d1 d21

namespace {

BoardingPlan plan(double boardingAtSec, double etdSec, int seqno = 1,
                  int pax = 0) {
  BoardingPlan p;
  p.valid = true;
  p.seqno = seqno;
  p.boardingAtSec = secondOfDay(boardingAtSec);
  p.etdSec = secondOfDay(etdSec);
  p.pax = pax;
  p.summary = "test plan";
  return p;
}

double hms(int h, int m, int s = 0) { return h * 3600.0 + m * 60.0 + s; }

}  // namespace

TEST_CASE(
    "Run1: ToLiss AUTO closing L1 mid-boarding, then AUTO -> OPEN, doesn't end "
    "boarding") {
  // run1 2026-11-01 a320neo eddm dlh432 plan at 03:12z for 03:26z etd 04:00z l1
  // open from load power at 105s cabin call 678s auto closed l1 794-800s owner
  // set it open at 821s 0.2.0 played boardingcomplete at 809s
  Flight f;
  f.purser.setAircraft(makeAircraftType(Family::A320, true));
  scheduleMode(f, hms(3, 12, 9));
  f.s.pax = 121;
  f.s.acPower = false;
  f.purser.setBoardingPlan(plan(hms(3, 26), hms(4, 0), 1, 121));
  f.run(105, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  f.run(573, [](SimState& x) { x.acPower = true; });
  CHECK(f.out.played.empty());  // waiting for 03:26z
  f.purser.cabinCall();
  f.run(116);  // t 794
  REQUIRE(f.purser.stageLabel() == "Boarding");
  const double end = f.purser.boardingEndsAt();
  // early start schedule etd 10 min capped at 1.5x natural no warning
  CHECK(end - 678.25 > 872);
  CHECK(f.purser.boardingNote().empty());
  f.run(6,
        [](SimState& x) { x.doorMax = 0, x.doorFwd = 0; });  // auto closes l1
  f.run(21);
  f.run(30, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });  // set to open
  f.run(300,
        [](SimState& x) { x.doorMax = 0, x.doorFwd = 0; });  // and shut again
  CHECK(f.count("BoardingComplete") == 0);
  CHECK(f.purser.stageLabel() == "Boarding");
  CHECK(f.purser.boardingEndsAt() ==
        doctest::Approx(end));  // door events never move the end
  REQUIRE(!f.out.music.empty());
  CHECK(f.out.music.back().second);  // music never stopped
  REQUIRE(f.runUntil([&] { return f.count("BoardingComplete") == 1; }, 2400));
  CHECK(f.t == doctest::Approx(end).epsilon(0.001));
  f.run(60,
        [](SimState& x) { x.doorMax = 1; });  // reopened after boardingcomplete
  CHECK(f.purser.stageLabel() == "Boarding complete");  // stays complete
  f.run(20, [](SimState& x) { x.beacon = true; });
  f.run(150, [](SimState& x) { x.doorMax = 0; });
  CHECK(f.count("ArmDoors") == 1);
  CHECK(f.count("BoardingComplete") == 1);
}

TEST_CASE(
    "boarding: the natural length without a plan; beacon, pushback, engines or "
    "slides end it sooner") {
  // 150 pax a321 factor 1.25 300 6 x 150 x 1.25 1500s plus minus 15 pct
  Flight f;
  f.run(5);
  f.run(20, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  REQUIRE(f.purser.stageLabel() == "Boarding");
  const double len = f.purser.boardingEndsAt() - f.t;
  CHECK(len > 1275 - 10);
  CHECK(len < 1725);
  CHECK(f.purser.nextLabel().find("BoardingWelcome") != std::string::npos);
  bool listed = false;
  for (const std::string& p : f.purser.pendingLabels())
    listed = listed || p.rfind("BoardingComplete in ", 0) == 0;
  CHECK(listed);

  const std::function<void(SimState&)> enders[] = {
      [](SimState& x) { x.beacon = true; },
      [](SimState& x) { x.gsKt = 2; },
      [](SimState& x) { x.enginesRunning = 1; },
      [](SimState& x) { x.slidesArmed = true; },
  };
  for (const auto& end : enders) {
    Flight g;
    g.run(5);
    g.run(60, [](SimState& x) { x.doorMax = 1; });
    REQUIRE(g.purser.stageLabel() == "Boarding");
    g.run(20, end);
    CHECK(g.count("BoardingComplete") == 1);
    CHECK(g.indexOf("BoardingComplete") < g.indexOf("ArmDoors"));
  }
}

TEST_CASE("a cabin call during boarding completes it within 60 s (D-21)") {
  Flight f;
  f.run(5);
  f.run(120, [](SimState& x) { x.doorMax = 1; });
  REQUIRE(f.purser.stageLabel() == "Boarding");
  f.purser.cabinCall();
  f.run(1);
  CHECK(f.purser.boardingEndsAt() <= f.t + 60);
  CHECK(f.runUntil([&] { return f.count("BoardingComplete") == 1; }, 62));
  CHECK(f.count("ArmDoors") == 0);  // armdoors still waits for the beacon
}

TEST_CASE(
    "schedule: a late start with a close ETD shortens boarding and warns "
    "(forgotten SimBrief OBT)") {
  // dispatched 22:00z w default obt 22:30z boarding 21:56z already passed
  // before load
  Flight f;
  f.purser.setAircraft(makeAircraftType(Family::A320, false));
  scheduleMode(f, hms(22, 4, 50) -
                      0.25);  // settle makes doors open at 22:05:05 like before
  f.purser.setBoardingPlan(plan(hms(21, 56), hms(22, 30)));
  f.run(5);
  f.run(16, [](SimState& x) { x.doorMax = 1; });  // doors rule
  REQUIRE(f.purser.stageLabel() == "Boarding");
  const double len = f.purser.boardingEndsAt() - f.t;
  CHECK(len == doctest::Approx(hms(22, 20) - hms(22, 5, 21))
                   .epsilon(0.01));  // to etd 10 min
  CHECK(f.purser.boardingNote().find("Boarding shortened to 15 min") !=
        std::string::npos);
  CHECK(f.purser.boardingNote().find("only 24 min away") != std::string::npos);

  Flight g;  // etd passed 5 min minimum
  scheduleMode(g, hms(22, 40));
  g.purser.setBoardingPlan(plan(hms(21, 56), hms(22, 30)));
  g.run(5);
  g.run(16, [](SimState& x) { x.doorMax = 1; });
  REQUIRE(g.purser.stageLabel() == "Boarding");
  CHECK(g.purser.boardingEndsAt() - g.t == doctest::Approx(300).epsilon(0.01));
  CHECK(g.purser.boardingNote().find("has passed") != std::string::npos);
  REQUIRE(g.runUntil([&] { return g.count("BoardingComplete") == 1; }, 310));

  Flight h;  // doors mode plan doesnt time boarding
  h.set.boardingStart = static_cast<int>(BoardingStart::Doors);
  h.s.zuluSec = hms(22, 5);
  h.purser.setBoardingPlan(plan(hms(21, 56), hms(22, 30)));
  h.run(5);
  h.run(16, [](SimState& x) { x.doorMax = 1; });
  CHECK(h.purser.boardingEndsAt() - h.t > 1275 - 10);
  CHECK(h.purser.boardingNote().empty());
}

TEST_CASE(
    "schedule: a boarding time over 60 min ahead means the X-Plane clock is "
    "off: doors rule and a warning") {
  Flight f;
  scheduleMode(f, hms(14, 0));
  f.purser.setBoardingPlan(plan(hms(21, 26), hms(22, 0)));
  f.run(5);
  CHECK(f.purser.boardingNote().find("7:26 h away") != std::string::npos);
  CHECK(f.purser.stageLabel() == "At the gate");
  f.run(20, [](SimState& x) { x.doorMax = 1; });
  REQUIRE(f.purser.stageLabel() == "Boarding");
  const double len = f.purser.boardingEndsAt() - f.t;
  CHECK(len > 1275 - 10);  // natural length etd meaningless w this clock
  CHECK(len < 1725);
  CHECK(f.purser.boardingNote().find("X-Plane clock") != std::string::npos);
}

TEST_CASE(
    "schedule: setting the X-Plane clock to the flight's time evaluates the "
    "plan again") {
  Flight f;
  scheduleMode(f, hms(14, 0));
  f.purser.setBoardingPlan(plan(hms(21, 26), hms(22, 0)));
  f.run(10);
  REQUIRE(!f.purser.boardingNote().empty());
  f.s.zuluSec = hms(21, 0);  // user sets the time
  f.run(5);
  CHECK(f.purser.boardingNote().empty());
  CHECK(f.purser.nextLabel().find("Boarding at 21:26Z") != std::string::npos);
  f.run(60, [](SimState& x) {
    x.doorMax = 1;
  });  // waiting now open doors dont start it
  CHECK(f.purser.stageLabel() == "At the gate");
}

TEST_CASE(
    "doors rule: ToLiss closed the door before AC power: boarding starts once "
    "power is on") {
  Flight f;
  f.s.acPower = false;
  f.run(60, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  f.run(300, [](SimState& x) {
    x.doorMax = 0, x.doorFwd = 0;
  });  // auto closes it still cold and dark
  CHECK(f.purser.stageLabel() == "At the gate");
  f.run(10, [](SimState& x) { x.acPower = true; });
  CHECK(f.purser.stageLabel() == "At the gate");
  f.run(10);
  CHECK(f.purser.stageLabel() == "Boarding");

  Flight g;  // never seen open no boarding w/o a call
  g.run(200);
  CHECK(g.purser.stageLabel() == "At the gate");
}

TEST_CASE(
    "turnaround: AUTO closing the door in the gap still boards at the gap end; "
    "a reopen ends the gap early") {
  auto landed = [](Flight& f) {
    f.set.turnaround = true;
    f.set.turnaroundGap = 300;
    f.run(5);
    f.boardAndDepart();
    f.run(5, [](SimState& x) { x.cabOn = true; });
    f.run(5, [](SimState& x) { x.cabOn = false; });
    f.takeoffAndClimb();
    f.descendAndLand();
    REQUIRE(f.purser.stageLabel() == "Turnaround");
  };
  Flight f;
  landed(f);
  const int welcomes = f.count("BoardingWelcome");
  f.run(100, [](SimState& x) {
    x.doorMax = 0, x.doorFwd = 0;
  });  // auto closes it during the gap
  CHECK(f.purser.stageLabel() == "Turnaround");
  CHECK(f.runUntil([&] { return f.purser.stageLabel() == "Boarding"; }, 300));
  f.run(60);
  CHECK(f.count("BoardingWelcome") > welcomes);

  Flight g;
  landed(g);
  g.run(30, [](SimState& x) { x.doorMax = 0, x.doorFwd = 0; });
  g.run(20, [](SimState& x) {
    x.doorMax = 1, x.doorFwd = 1;
  });  // closed then opened
  CHECK(g.purser.stageLabel() == "Boarding");
}

// toliss cross checks no fmgs estimate 4.1 4.2 d20

TEST_CASE(
    "takeoff thrust from the thrust levers when the FMGS stays at PreFlight") {
  Flight f;
  f.run(5);
  f.run(30, [](SimState& x) { x.doorMax = 1; });
  f.run(60);
  f.run(20, [](SimState& x) {
    x.doorMax = 0, x.beacon = true, x.enginesRunning = 2, x.gsKt = 2;
  });
  f.run(75);
  REQUIRE(f.count("SafetyBriefing") == 1);
  f.run(5, [](SimState& x) {
    x.gsKt = 8, x.throttleMax = 0.88f;
  });  // flx rolling apphase still 0
  CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Stopped);
}

TEST_CASE("takeoff thrust from the ECAM flight phase") {
  Flight f;
  f.run(5);
  f.run(30, [](SimState& x) { x.doorMax = 1; });
  f.run(60);
  f.run(20, [](SimState& x) {
    x.doorMax = 0, x.beacon = true, x.enginesRunning = 2, x.gsKt = 2;
  });
  f.run(75);
  REQUIRE(f.count("SafetyBriefing") == 1);
  f.run(2, [](SimState& x) { x.ecamPhase = 3; });  // to power not rolling yet
  CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Stopped);
}

TEST_CASE("thrust levers below the FLX detent are not takeoff thrust") {
  Flight f;
  f.run(5);
  f.run(30, [](SimState& x) { x.doorMax = 1; });
  f.run(60);
  f.run(20, [](SimState& x) {
    x.doorMax = 0, x.beacon = true, x.enginesRunning = 2, x.gsKt = 2;
  });
  f.run(75);
  REQUIRE(f.count("SafetyBriefing") == 1);
  f.run(10,
        [](SimState& x) { x.gsKt = 15, x.throttleMax = 0.3f; });  // taxi thrust
  CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Playing);
}

TEST_CASE(
    "no FMGS: an estimated phase still gives AfterTakeoff, CabinService and "
    "DescentStarted") {
  Flight f;
  f.run(5);
  f.boardAndDepart();
  f.run(5, [](SimState& x) { x.cabOn = true; });
  f.run(5, [](SimState& x) { x.cabOn = false; });
  // apphase stuck at 0 all flight qpac altitude drive the estimate
  f.run(30, [](SimState& x) {
    x.gsKt = 140, x.throttleMax = 0.88f, x.ecamPhase = 4;
  });
  f.run(20, [](SimState& x) {
    x.onGround = false, x.aglFt = 800, x.vsFpm = 2500, x.ecamPhase = 5,
    x.qpacPhase = 6;
  });
  CHECK(!f.purser.phaseEstimated());  // 30s after liftoff
  f.run(200, [](SimState& x) {
    x.aglFt = 8000, x.mslFt = 8000, x.qpacPhase = 8, x.throttleMax = 0.7f;
  });
  CHECK(f.purser.phaseEstimated());
  CHECK(f.count("AfterTakeoff") == 1);
  f.run(300, [](SimState& x) { x.aglFt = 30000, x.mslFt = 30000; });
  f.run(900, [](SimState& x) {
    x.vsFpm = 0, x.aglFt = 34000, x.mslFt = 34000;
  });  // level no cruise alt
  CHECK(f.purser.phase() == Phase::Cruise);
  CHECK(f.count("CabinService") == 1);
  f.run(300,
        [](SimState& x) { x.vsFpm = -2000, x.aglFt = 20000, x.mslFt = 20000; });
  CHECK(f.purser.phase() == Phase::Descent);
  CHECK(f.count("DescentStarted") == 1);
  f.run(400, [](SimState& x) {
    x.aglFt = 3000, x.mslFt = 3000, x.qpacPhase = 9, x.gearLeverDown = 1;
  });
  CHECK(f.purser.phase() == Phase::Approach);
  CHECK(f.count("CrewTakeSeats(landing)") == 1);
}

TEST_CASE("touchdown from the ECAM flight phase drops the landing calls") {
  Flight f;
  f.s.onGround = false;
  f.s.aglFt = 9000;
  f.s.mslFt = 9000;
  f.phase(Phase::Descent);
  f.run(150, [](SimState& x) { x.vsFpm = -1000; });
  f.phase(Phase::Approach);
  f.run(5, [](SimState& x) { x.aglFt = 20; });
  f.run(5, [](SimState& x) {
    x.ecamPhase = 8;
  });  // gear ref hasnt said on ground yet
  f.run(150, [](SimState& x) {
    x.onGround = true, x.aglFt = 0, x.gsKt = 20, x.ecamPhase = 9;
  });
  CHECK(f.count("AfterLanding") == 1);
}

// 0.2.2 stuff departure pacing pilot signals landing hot start turnaround
// switch

TEST_CASE(
    "the safety briefing waits for the aircraft to move: 20-40 s after, and 60 "
    "s after ArmDoors (D-22)") {
  Flight f;
  f.run(5);
  f.run(30, [](SimState& x) { x.doorMax = 1; });
  f.run(60);
  f.run(20, [](SimState& x) {
    x.doorMax = 0, x.beacon = true;
  });  // armdoors at the gate no tug yet
  REQUIRE(f.count("ArmDoors") == 1);
  f.run(600);
  CHECK(f.count("PreSafetyBriefing") == 0);
  bool listed = false;
  for (const std::string& l : f.purser.pendingLabels())
    listed = listed || l.find("when the aircraft moves") != std::string::npos;
  CHECK(listed);
  const double push = f.t;
  f.run(0.25, [](SimState& x) { x.gsKt = 2; });
  REQUIRE(f.runUntil([&] { return f.count("PreSafetyBriefing") == 1; }, 60));
  CHECK(f.t - push >= 20 - 0.5);
  CHECK(f.t - push <= 40 + 0.5);

  Flight g;  // pushed right away still at least 60s after armdoors
  g.run(5);
  g.run(30, [](SimState& x) { x.doorMax = 1; });
  g.run(60);
  g.run(0.25, [](SimState& x) { x.doorMax = 0, x.beacon = true, x.gsKt = 2; });
  const double armed = g.t;
  REQUIRE(g.runUntil([&] { return g.count("PreSafetyBriefing") == 1; }, 200));
  CHECK(g.t - armed >=
        60 + 8 - 1);  // boardingcomplete armdoors 8s each then 60s
}

TEST_CASE(
    "line-up: strobe ON (not AUTO) or a cabin call once taxiing fire seats for "
    "takeoff (D-23)") {
  Flight f;
  f.run(5);
  f.boardAndDepart();
  f.answerCab();
  f.run(30, [](SimState& x) { x.strobeOn = 0; });  // strobe auto
  CHECK(f.count("CrewTakeSeats(takeoff)") == 0);
  f.run(10, [](SimState& x) { x.strobeOn = 1; });
  CHECK(f.count("CrewTakeSeats(takeoff)") == 1);

  Flight g;  // boardanddepart taxis at 15 kt engines on
  g.run(5);
  g.boardAndDepart();
  g.answerCab();
  REQUIRE(g.count("CrewTakeSeats(takeoff)") == 0);
  g.purser.cabinCall();
  g.run(10);
  CHECK(g.count("CrewTakeSeats(takeoff)") == 1);
}

TEST_CASE(
    "line-up: an early signal hurries the cabin check; seats follow "
    "CabinSecured (D-23)") {
  Flight f;
  f.set.expediteSeconds = 10;
  f.run(5);
  f.run(20,
        [](SimState& x) { x.beacon = true, x.enginesRunning = 2, x.gsKt = 2; });
  REQUIRE(f.runUntil([&] { return f.count("SafetyBriefing") == 1; }, 200));
  const double start = f.t;
  f.run(5, [](SimState& x) {
    x.landingLights = true;
  });  // lights on during the briefing
  REQUIRE(f.runUntil([&] { return f.purser.calling(); }, 400));
  CHECK(f.t - start < 150 + 10 + 4);
  CHECK(f.lastOutcome(AnnType::SafetyBriefing) ==
        Outcome::Played);                         // briefing plays out
  CHECK(f.count("CrewTakeSeats(takeoff)") == 0);  // behind the cabins call
  f.answerCab();
  CHECK(f.runUntil([&] { return f.count("CrewTakeSeats(takeoff)") == 1; }, 30));
}

TEST_CASE(
    "line-up: without a signal before takeoff thrust, CrewTakeSeats expires "
    "(D-23)") {
  Flight f;
  f.run(5);
  f.boardAndDepart();
  f.answerCab();
  f.phase(Phase::Takeoff);
  f.run(10, [](SimState& x) { x.gsKt = 60; });
  CHECK(f.count("CrewTakeSeats(takeoff)") == 0);
  CHECK(f.lastOutcome(AnnType::CrewTakeSeats, CallContext::Takeoff) ==
        Outcome::Expired);
}

TEST_CASE(
    "climb: a NO SMOKING flick or a cabin call means crew may leave seats: "
    "AfterTakeoff now (D-25)") {
  for (int useCall = 0; useCall < 2; ++useCall) {
    Flight f;
    f.s.noSmoking = 1;
    f.run(5);
    f.boardAndDepart();
    f.answerCab();
    f.lineUp();
    f.phase(Phase::Takeoff);  // fmgs stuck in takeoff phase so aftertakeoff
                              // wouldnt come by itself
    f.run(30, [](SimState& x) { x.gsKt = 140; });
    f.run(20, [](SimState& x) {
      x.onGround = false, x.aglFt = 2200, x.vsFpm = 2000;
    });
    REQUIRE(f.count("AfterTakeoff") == 0);
    if (useCall) {
      f.purser.cabinCall();
    } else {
      f.run(3, [](SimState& x) { x.noSmoking = 0; });
      f.s.noSmoking = 1;
    }
    f.run(10);
    CHECK(f.count("AfterTakeoff") == 1);
  }
}

TEST_CASE(
    "landing: CabinSecured in the approach window; seats on a NO SMOKING "
    "flick, not a slow one (D-24)") {
  Flight f;
  f.s.onGround = false;
  f.s.aglFt = 12000;
  f.s.mslFt = 12000;
  f.s.noSmoking = 1;
  f.s.gearLeverDown = 0;
  f.phase(Phase::Cruise);
  f.run(10);
  f.phase(Phase::Descent);
  f.run(150, [](SimState& x) {
    x.vsFpm = -1000, x.aglFt = 8000;
  });  // descentstarted then the landing check
  REQUIRE(f.count("DescentStarted") == 1);
  REQUIRE(f.runUntil([&] { return f.purser.calling(); }, 300));
  f.answerCab();
  CHECK(f.count("CabinSecured(landing)#ip") == 1);
  f.run(60);
  CHECK(f.count("CrewTakeSeats(landing)") == 0);    // waits for the signal
  f.run(20, [](SimState& x) { x.noSmoking = 0; });  // off for 20s isnt a flick
  f.run(5, [](SimState& x) { x.noSmoking = 1; });
  CHECK(f.count("CrewTakeSeats(landing)") == 0);
  f.run(3, [](SimState& x) { x.noSmoking = 0; });
  f.run(10, [](SimState& x) { x.noSmoking = 1; });
  CHECK(f.count("CrewTakeSeats(landing)") == 1);
}

TEST_CASE(
    "landing: below 1,000 ft the gear down no longer brings the seats call "
    "(D-24)") {
  Flight f;
  f.s.onGround = false;
  f.s.aglFt = 12000;
  f.s.mslFt = 12000;
  f.s.gearLeverDown = 0;
  f.phase(Phase::Cruise);
  f.run(10);
  f.phase(Phase::Descent);
  f.run(150, [](SimState& x) { x.vsFpm = -1000, x.aglFt = 8000; });
  REQUIRE(f.runUntil([&] { return f.purser.calling(); }, 300));
  f.answerCab();
  f.phase(Phase::Approach);
  f.run(30, [](SimState& x) { x.aglFt = 800; });
  f.run(30, [](SimState& x) { x.gearLeverDown = 1; });
  CHECK(f.count("CrewTakeSeats(landing)") == 0);
}

TEST_CASE(
    "APPhase back to PreFlight after engine shutdown is not an uninitialized "
    "FMGS (Run2)") {
  Flight f;
  f.run(5);
  f.boardAndDepart();
  f.answerCab();
  f.lineUp();
  f.takeoffAndClimb();
  f.descendAndLand();         // done at touchdown
  f.phase(Phase::PreFlight);  // toliss at shutdown
  f.run(120);
  CHECK(!f.purser.phaseEstimated());
}

TEST_CASE(
    "hot start: parked with engines running skips boarding until they are shut "
    "down at the gate") {
  Flight f;
  f.s.enginesRunning = 2;
  f.s.parkBrake = true;
  f.run(5);
  CHECK(f.purser.boardingNote().find("Shut the engines down at the gate") !=
        std::string::npos);
  f.purser.cabinCall();
  f.run(60, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  CHECK(f.count("BoardingWelcome") == 0);
  CHECK(f.count("PreSafetyBriefing") ==
        0);  // parked departure waits for movement
  f.run(30, [](SimState& x) {
    x.enginesRunning = 0;
  });  // shut down at the gate doors open doors rule
  CHECK(f.purser.boardingNote().empty());
  CHECK(f.runUntil([&] { return f.purser.stageLabel() == "Boarding"; }, 30));
}

TEST_CASE(
    "settle: groundspeed while X-Plane settles the aircraft is not taken for "
    "movement") {
  Flight f;
  f.settled = true;                           // drives the settle itself
  f.run(2, [](SimState& x) { x.gsKt = 3; });  // first ticks after a load
  f.run(20, [](SimState& x) { x.gsKt = 0; });
  CHECK(f.purser.stageLabel() == "At the gate");
  f.run(30, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  CHECK(f.purser.stageLabel() == "Boarding");
}

TEST_CASE(
    "turnaround off: flight complete; a new SimBrief OFP from here starts the "
    "next flight (D-26)") {
  Flight f;  // turnaround off by default
  f.purser.setBoardingPlan(planAt(
      hms(23, 0), 1));  // ofp this flight departs with doors mode not timing
  f.run(5);
  f.boardAndDepart();
  f.answerCab();
  f.lineUp();
  f.purser.setBoardingPlan(
      BoardingPlan{});  // away from its origin it stops passing
  f.takeoffAndClimb();
  f.descendAndLand();
  REQUIRE(f.count("DeboardingComplete") == 1);
  f.run(10);
  CHECK(f.purser.flightComplete());
  CHECK(f.purser.stageLabel() == "Flight complete");
  CHECK(f.purser.packLocked());
  const int welcomes = f.count("BoardingWelcome");
  f.run(600);  // doors open but the flight is over
  CHECK(f.count("BoardingWelcome") == welcomes);
  f.purser.setBoardingPlan(planAt(
      hms(23, 0), 1));  // same ofp passing again at its origin no new flight
  f.run(30);
  CHECK(f.purser.flightComplete());
  f.purser.setBoardingPlan(planAt(hms(23, 0), 2));  // next legs ofp
  f.run(5);
  CHECK(!f.purser.flightComplete());
  CHECK(!f.purser.packLocked());
  CHECK(f.runUntil([&] { return f.purser.stageLabel() == "Boarding"; }, 60));
}

// recorded flights local only runs/ never committed

TEST_CASE("replay of recorded traces in ../runs (local only)") {
  namespace fs = std::filesystem;
  const fs::path runs = fs::path(FXA_SOURCE_DIR) / ".." / "runs";
  std::error_code ec;
  if (!fs::is_directory(runs, ec)) {
    MESSAGE("no runs/ folder: skipped");
    return;
  }
  int traces = 0;
  for (const auto& e : fs::recursive_directory_iterator(runs, ec)) {
    if (!e.is_regular_file() ||
        e.path().string().find(".fxtrace.csv") == std::string::npos)
      continue;
    std::ifstream in(e.path(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const std::vector<TraceRow> rows = readTrace(ss.str());
    if (rows.empty()) continue;
    ++traces;
    // what the purser did in the sim one entry per type change
    std::vector<std::string> recorded;
    bool calls = false;
    for (const TraceRow& r : rows) {
      if (!r.isEvent) continue;
      if (r.event.rfind("call button ", 0) == 0 &&
          r.event.find("/mech") == std::string::npos)
        calls = true;
      const size_t sp = r.event.find(" playing");
      if (sp == std::string::npos) continue;
      const std::string type = r.event.substr(0, sp);
      if (recorded.empty() || recorded.back() != type) recorded.push_back(type);
    }
    FakeOut out;
    double clock = 0;
    out.clock = &clock;
    Purser p(out, 7);
    Settings set;
    set.boardingStart =
        static_cast<int>(calls ? BoardingStart::Call : BoardingStart::Doors);
    const SimState& first = rows.front().state;
    const Family fam = familyFrom(first.acfIcao, "");
    p.setAircraft(
        makeAircraftType(fam == Family::Unknown ? Family::A320 : fam,
                         first.acfIcao == "A20N" || first.acfIcao == "A21N"));
    // decisions announcements w replay times
    std::vector<std::string> story;
    p.setNoteSink([&](double at, const std::string& text) {
      story.push_back(std::to_string(static_cast<int>(at)) + "  " + text);
    });
    p.setEventSink([&](const TimelineRow& row) {
      if (row.outcome == Outcome::Playing || row.outcome == Outcome::Expired ||
          row.outcome == Outcome::Unanswered || row.outcome == Outcome::NoSound)
        story.push_back(std::to_string(static_cast<int>(row.t)) + "  " +
                        annTypeName(row.type) +
                        (row.context != CallContext::Any
                             ? std::string("(") + contextName(row.context) + ")"
                             : "") +
                        " " + outcomeName(row.outcome));
    });
    // older traces miss strobe/ns/gear cols and 0.2.1 decoded spoilers wrong
    // pull them from the raw values the recorder logged
    int rawStrobe = -1, rawNs = -1, rawGear = -1, rawSpoilers = -2;
    auto rawValues = [](const std::string& ev,
                        const char* name) -> std::optional<std::vector<float>> {
      const std::string head = std::string("raw ") + name + " = ";
      if (ev.rfind(head, 0) != 0) return std::nullopt;
      std::vector<float> v;
      std::istringstream in(ev.substr(head.size()));
      for (float x; in >> x;) v.push_back(x);
      return v;
    };
    double last = -1;
    for (const TraceRow& r : rows) {
      if (r.isEvent) {
        if (r.event.rfind("call button ", 0) == 0 &&
            r.event.find("/mech") == std::string::npos)
          p.cabinCall();
        if (auto v = rawValues(r.event, "AirbusFBW/OHPLightSwitches"))
          rawStrobe = switchAt(*v, set.strobeSwitchIndex, 2);
        if (auto v = rawValues(r.event, "AirbusFBW/NoSmokingSignsOn");
            v && !v->empty())
          rawNs = v->front() > 0.5f ? 1 : 0;
        if (auto v = rawValues(r.event, "AirbusFBW/GearLever");
            v && !v->empty())
          rawGear = v->front() > 0.5f ? 1 : 0;
        if (auto v = rawValues(r.event, "AirbusFBW/SDSpoilerArray"))
          rawSpoilers = groundSpoilersFromSd(*v);
        continue;
      }
      // recorded at 1hz tick 4hz in between so timers run like in the sim
      SimState s = r.state;
      if (s.strobeOn < 0) s.strobeOn = rawStrobe;
      if (s.noSmoking < 0) s.noSmoking = rawNs;
      if (s.gearLeverDown < 0) s.gearLeverDown = rawGear;
      if (rawSpoilers > -2) s.groundSpoilers = rawSpoilers;
      const double from = last < 0 ? s.t : last;
      for (double t = from + 0.25; t <= s.t + 1e-6; t += 0.25) {
        clock = t;
        s.t = t;
        p.tick(s, set);
      }
      last = r.state.t;
    }
    // old traces bake in old rules 0.2.0 ended boarding on the auto door so
    // only check flight sequence invariants not the recorded calls
    std::vector<std::string> replayed;
    for (const std::string& label : out.played)
      replayed.push_back(label.substr(0, label.find_first_of("#")));
    const std::string name = e.path().filename().string();
    int legs = 1, boardingComplete = 0, armDoors = 0;
    bool afterComplete = false;
    for (const std::string& type : replayed) {
      if (type == "DeboardingComplete") {
        ++legs;
        afterComplete = false;
      }
      if (type == "BoardingComplete") {
        ++boardingComplete;
        afterComplete = true;
      }
      if (type == "ArmDoors") {
        ++armDoors;
        const std::string msg = name + ": ArmDoors before BoardingComplete";
        CHECK_MESSAGE(afterComplete, msg);
      }
      if (type == "BoardingWelcome") {
        const std::string msg =
            name + ": BoardingWelcome after BoardingComplete";
        CHECK_MESSAGE(!afterComplete, msg);
      }
    }
    const std::string once =
        name + ": BoardingComplete or ArmDoors more than once per leg";
    CHECK_MESSAGE(boardingComplete <= legs, once);
    CHECK_MESSAGE(armDoors <= legs, once);
    const std::string summary =
        name + ": recorded " + std::to_string(recorded.size()) +
        " announcement types, replayed " + std::to_string(replayed.size()) +
        " announcements";
    MESSAGE(summary);
    if (std::getenv("FXA_REPLAY_TIMELINE")) {  // the replay to compare
      std::string text = name + " replayed:\n";
      for (const std::string& line : story) text += "  " + line + "\n";
      MESSAGE(text);
    }
  }
  const std::string done = std::to_string(traces) + " recorded traces replayed";
  MESSAGE(done);
}

TEST_CASE(
    "the Coming-up list is structured; timeline rows carry zulu and local time "
    "(Timeline tab, 0.3.0)") {
  Flight f;
  f.s.zuluSec = 13 * 3600 + 30 * 60;  // 13:30z
  f.s.localTimeSec = 15 * 3600 + 30 * 60;
  f.run(5);
  f.run(20, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
  REQUIRE(f.purser.stageLabel() == "Boarding");
  const auto items = f.purser.pending();
  const auto lines = f.purser.pendingLabels();
  REQUIRE(items.size() == lines.size());
  REQUIRE(!items.empty());
  // timed first soonest first conditions after
  bool seenCondition = false;
  double last = -1;
  for (const PendingItem& p : items) {
    if (p.inSec < 0) {
      seenCondition = true;
      CHECK(!p.condition.empty());
    } else {
      CHECK(!seenCondition);
      CHECK(p.inSec >= last);
      last = p.inSec;
    }
  }
  CHECK(seenCondition);
  REQUIRE(items.front().type);
  CHECK(*items.front().type == AnnType::BoardingWelcome);
  CHECK(annLabel(*items.front().type, items.front().context) ==
        "Boarding welcome");
  bool sooner = false;
  for (const PendingItem& p : items)
    sooner = sooner || (p.type == AnnType::BoardingComplete && p.inSec < 0);
  CHECK(sooner);

  REQUIRE(f.runUntil([&] { return !f.purser.timeline().empty(); }, 600));
  const TimelineRow& r = f.purser.timeline().back();
  CHECK(r.zuluSec > 13 * 3600 + 30 * 60);
  CHECK(r.zuluSec < 14 * 3600);
  CHECK(r.localSec >= 15 * 3600 + 30 * 60);  // fixture holds local time still
  CHECK(r.localSec < 16 * 3600);
}

TEST_CASE("announcement labels and where they are heard (0.3.0)") {
  for (int k = 0; k < kAnnTypeCount; ++k) {
    const AnnType t = static_cast<AnnType>(k);
    const std::string label = annTypeLabel(t);
    CHECK(!label.empty());
    CHECK(label != "?");
    CHECK(label.find_first_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ", 1) ==
          std::string::npos);  // sentence case
    CHECK(std::string(heardOn(t, defaultMode(t))).size() > 0);
  }
  CHECK(annLabel(AnnType::CrewTakeSeats, CallContext::Landing) ==
        "Crew take seats (landing)");
  CHECK(annFenixLabel(AnnType::CrewTakeSeats, CallContext::Landing) ==
        "CrewTakeSeats (landing)");
  CHECK(std::string(heardOn(AnnType::CabinSecured, Mode::Interphone)) ==
        "Interphone");
  CHECK(std::string(heardOn(AnnType::BoardingMusic, Mode::Manual)) == "Music");
  CHECK(std::string(heardOn(AnnType::SafetyBriefing, Mode::Prerecorded)) ==
        "Cabin PA");
}
