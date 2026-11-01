// Purser tests: whole flights through the real state machine with a fake player (VP_SPEC.md § 4).
#include "core/purser.h"
#include "core/recorder.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>

using namespace fxa;

namespace {

struct FakeOut : PurserOutput {
    const double* clock = nullptr;
    std::set<AnnType> missing;                 // types the "pack" lacks
    std::map<AnnType, double> durations;       // default 8 s
    std::vector<std::string> played;           // "Type" or "Type(takeoff)"
    std::vector<std::pair<double, bool>> music;
    int chimes = 0;
    double endsAt = -1;
    int stops = 0;

    std::optional<std::string> play(AnnType t, CallContext c, bool interphone) override
    {
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
    void stop() override
    {
        endsAt = -1;
        ++stops;
    }
    void chime() override { ++chimes; }
    void setMusic(bool on) override { music.emplace_back(*clock, on); }
    std::optional<Mode> peek(AnnType t, CallContext) const override
    {
        if (missing.count(t)) return std::nullopt;
        return t == AnnType::CabinSecured ? Mode::Interphone : Mode::Manual;
    }
};

struct Flight {
    FakeOut out;
    Settings set;
    SimState s;
    Purser purser{ out, 42 };
    double t = 0;

    Flight()
    {
        out.clock = &t;
        out.durations[AnnType::SafetyBriefing] = 150;
        out.durations[AnnType::BoardingMusic] = 0;
        set.chainGap = 2;
        set.boardingStart = static_cast<int>(BoardingStart::Doors);   // these flights board when the doors open
        s.onGround = true;
        s.acPower = true;
        s.pax = 150;
        s.slidesKnown = true;
        s.sunPitchDeg = 30;
        s.fmgsPhase = Phase::PreFlight;
        s.fmgsPhaseRaw = 0;
        purser.setAircraft(makeAircraftType(Family::A321, true));
    }
    // Holds the current state (after `change`) for `seconds`, ticking at 4 Hz.
    void run(double seconds, const std::function<void(SimState&)>& change = {})
    {
        if (change) change(s);
        for (double e = 0; e < seconds; e += 0.25) {
            t += 0.25;
            s.t = t;
            if (s.zuluSec >= 0) s.zuluSec = secondOfDay(s.zuluSec + 0.25);
            purser.tick(s, set);
        }
    }
    // Ticks until pred() holds (true) or maxSeconds pass (false).
    bool runUntil(const std::function<bool()>& pred, double maxSeconds)
    {
        for (double e = 0; e < maxSeconds; e += 0.25) {
            if (pred()) return true;
            run(0.25);
        }
        return pred();
    }
    void phase(Phase p) { s.fmgsPhase = p, s.fmgsPhaseRaw = static_cast<int>(p); }
    int count(const std::string& label) const { return static_cast<int>(std::count(out.played.begin(), out.played.end(), label)); }
    size_t indexOf(const std::string& label) const
    {
        return static_cast<size_t>(std::find(out.played.begin(), out.played.end(), label) - out.played.begin());
    }
    Outcome lastOutcome(AnnType type, CallContext c = CallContext::Any) const
    {
        const auto& tl = purser.timeline();
        for (auto r = tl.rbegin(); r != tl.rend(); ++r)
            if (r->type == type && r->context == c) return r->outcome;
        return Outcome::Queued;
    }

    // ---- flight segments ----
    void boardAndDepart()
    {
        run(30, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });   // doors open at the gate
        run(600);                                                       // boarding, welcomes every 2-6 min
        run(20, [](SimState& x) { x.doorMax = 0, x.doorFwd = 0; });   // ToLiss AUTO doors close
        run(10, [](SimState& x) { x.beacon = true, x.seatbelt = true; });
        run(60, [](SimState& x) { x.gsKt = 2; });                      // pushback
        run(60, [](SimState& x) { x.enginesRunning = 2, x.gsKt = 0; });
        s.gsKt = 15;                                                     // taxi until the cabin calls the cockpit
        runUntil([&] { return purser.calling(); }, 600);
    }
    void takeoffAndClimb()
    {
        phase(Phase::Takeoff);
        run(30, [](SimState& x) { x.gsKt = 140; });
        run(20, [](SimState& x) { x.onGround = false, x.aglFt = 800, x.vsFpm = 2500; });
        phase(Phase::Climb);
        run(200, [](SimState& x) { x.aglFt = 8000; });
        run(30, [](SimState& x) { x.seatbelt = false, x.aglFt = 12000; });   // signs off in the climb
        phase(Phase::Cruise);
        run(900, [](SimState& x) { x.aglFt = 36000, x.mslFt = 36000, x.vsFpm = 0; });
    }
    void descendAndLand()
    {
        phase(Phase::Descent);
        run(300, [](SimState& x) { x.vsFpm = -2000, x.aglFt = 20000, x.mslFt = 20000, x.seatbelt = true; });
        run(400, [](SimState& x) { x.aglFt = 9000, x.mslFt = 9000; });
        phase(Phase::Approach);
        run(300, [](SimState& x) { x.aglFt = 3000, x.mslFt = 3000; });
        run(30, [](SimState& x) { x.aglFt = 500; });
        phase(Phase::Done);
        run(20, [](SimState& x) { x.onGround = true, x.aglFt = 0, x.gsKt = 120, x.speedbrake = 1, x.landingLights = true; });
        run(150, [](SimState& x) { x.gsKt = 15, x.speedbrake = 0, x.landingLights = false; });   // taxi in
        run(30, [](SimState& x) { x.gsKt = 0, x.parkBrake = true, x.enginesRunning = 0, x.beacon = false; });
        run(400, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });
    }
};

} // namespace

TEST_CASE("a normal flight plays every call in order")
{
    Flight f;
    f.run(5);
    f.boardAndDepart();
    CHECK(f.count("BoardingWelcome") >= 2);
    CHECK(f.count("BoardingComplete") == 1);
    CHECK(f.count("ArmDoors") == 1);
    CHECK(f.count("PreSafetyBriefing") == 1);
    CHECK(f.count("SafetyBriefing") == 1);
    CHECK(f.count("CrewTakeSeats(takeoff)") == 1);
    CHECK(f.count("CabinDim(takeoff)") == 0);   // daylight
    CHECK(f.out.chimes >= 1);
    REQUIRE(f.purser.calling());               // waiting for the CAB knob
    f.run(5, [](SimState& x) { x.cabOn = true; });
    CHECK(f.count("CabinSecured(takeoff)#ip") == 1);
    f.run(10, [](SimState& x) { x.cabOn = false; });
    CHECK(f.purser.packLocked());

    f.takeoffAndClimb();
    CHECK(f.count("AfterTakeoff") == 1);
    CHECK(f.count("SeatbeltSignOff") == 1);
    CHECK(f.count("CabinService") == 1);
    CHECK(f.indexOf("AfterTakeoff") < f.indexOf("SeatbeltSignOff"));

    f.descendAndLand();
    CHECK(f.count("DescentStarted") == 1);
    CHECK(f.count("SeatbeltSignOn") == 0);     // covered by the descent announcement
    CHECK(f.count("CrewTakeSeats(landing)") == 1);
    CHECK(f.lastOutcome(AnnType::CabinSecured, CallContext::Landing) == Outcome::Unanswered);   // nobody pulled CAB
    CHECK(f.count("AfterLanding") == 1);
    CHECK(f.count("DisarmDoors") == 1);
    CHECK(f.count("DeboardingStarted") == 1);
    CHECK(f.count("DeboardingComplete") == 1);

    // Boarding music during boarding and deboarding only.
    REQUIRE(f.out.music.size() >= 4);
    CHECK(f.out.music[0].second == true);
    CHECK(f.out.music[1].second == false);
    CHECK(f.out.music[2].second == true);
    CHECK(f.out.music[3].second == false);

    const std::vector<std::string> order = { "BoardingComplete", "ArmDoors", "PreSafetyBriefing", "SafetyBriefing",
                                             "CrewTakeSeats(takeoff)", "CabinSecured(takeoff)#ip", "AfterTakeoff", "CabinService",
                                             "DescentStarted", "CrewTakeSeats(landing)", "AfterLanding", "DisarmDoors",
                                             "DeboardingStarted", "DeboardingComplete" };
    for (size_t k = 1; k < order.size(); ++k) CHECK_MESSAGE(f.indexOf(order[k - 1]) < f.indexOf(order[k]), order[k]);
}

TEST_CASE("unanswered cabin call: chime plus 3 retries, then the cabin gives up")
{
    Flight f;
    f.run(5);
    f.boardAndDepart();
    REQUIRE(f.purser.calling());
    const int before = f.out.chimes;
    f.run(200);
    CHECK(f.out.chimes - before == 3);
    CHECK(f.lastOutcome(AnnType::CabinSecured, CallContext::Takeoff) == Outcome::Unanswered);
    CHECK(f.count("CabinSecured(takeoff)#ip") == 0);
}

TEST_CASE("takeoff thrust stops the safety briefing and drops the pre-takeoff calls")
{
    Flight f;
    f.run(5);
    f.run(30, [](SimState& x) { x.doorMax = 1; });
    f.run(60);
    f.run(20, [](SimState& x) { x.doorMax = 0, x.beacon = true, x.enginesRunning = 2; });
    f.run(75);   // ArmDoors + 60 s, the safety briefing starts
    REQUIRE(f.count("SafetyBriefing") == 1);
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

TEST_CASE("missing sounds are skipped and the chain continues")
{
    Flight f;
    f.out.missing = { AnnType::PreSafetyBriefing, AnnType::BoardingWelcome, AnnType::CabinSecured };
    f.run(5);
    f.boardAndDepart();
    CHECK(f.count("SafetyBriefing") == 1);
    CHECK(f.count("CrewTakeSeats(takeoff)") == 1);
    CHECK(f.out.chimes == 0);   // no CabinSecured sound: no call (Fenix: no call without the announcement)
    CHECK(f.lastOutcome(AnnType::PreSafetyBriefing) == Outcome::NoSound);
}

TEST_CASE("disabled events are skipped")
{
    Flight f;
    f.set.disabledEvents = { "SafetyBriefing" };
    f.run(5);
    f.boardAndDepart();
    CHECK(f.count("SafetyBriefing") == 0);
    CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Disabled);
    CHECK(f.count("CrewTakeSeats(takeoff)") == 1);
}

TEST_CASE("darkness adds the cabin dim calls")
{
    Flight f;
    f.s.sunPitchDeg = -10;
    f.run(5);
    f.boardAndDepart();
    CHECK(f.count("CabinDim(takeoff)") == 1);
    CHECK(f.indexOf("CrewTakeSeats(takeoff)") < f.indexOf("CabinDim(takeoff)"));
}

TEST_CASE("decompression pre-empts everything and cancels cabin service")
{
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

TEST_CASE("starting airborne in cruise: no departure calls, the descent still comes")
{
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

TEST_CASE("starting on the runway with engines running skips boarding and the briefing")
{
    Flight f;
    f.s.enginesRunning = 2;
    f.s.gsKt = 5;
    f.s.beacon = true;
    f.run(120);
    CHECK(f.out.played.empty());
}

TEST_CASE("go-around drops pending landing calls without repeating them")
{
    Flight f;
    f.s.onGround = false;
    f.s.aglFt = 12000;
    f.s.mslFt = 12000;
    f.phase(Phase::Cruise);
    f.run(10);
    f.phase(Phase::Descent);
    f.run(150, [](SimState& x) { x.vsFpm = -1000, x.aglFt = 8000; });   // DescentStarted, then the landing cabin check
    REQUIRE(f.count("DescentStarted") == 1);
    f.phase(Phase::Approach);
    f.run(2, [](SimState& x) { x.aglFt = 2000; });
    f.phase(Phase::GA);
    f.run(60, [](SimState& x) { x.aglFt = 1500, x.vsFpm = 2000; });
    f.phase(Phase::Approach);
    f.run(400, [](SimState& x) { x.aglFt = 2500, x.vsFpm = -800; });
    CHECK(f.count("CrewTakeSeats(landing)") <= 1);
}

TEST_CASE("advance: boarding straight to BoardingComplete, then timers")
{
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
    f.purser.advance();   // pre-safety delay
    f.run(5);
    CHECK(f.count("PreSafetyBriefing") == 1);
    f.purser.skip();      // stop it; SafetyBriefing follows
    f.run(5);
    CHECK(f.lastOutcome(AnnType::PreSafetyBriefing) == Outcome::Stopped);
    CHECK(f.count("SafetyBriefing") == 1);
}

TEST_CASE("no AC power: nothing plays until power is on")
{
    Flight f;
    f.s.acPower = false;
    f.run(5);
    f.run(200, [](SimState& x) { x.doorMax = 1; });
    CHECK(f.out.played.empty());
    CHECK(f.out.music.empty());
    f.run(200, [](SimState& x) { x.acPower = true; });
    CHECK(f.count("BoardingWelcome") >= 1);
}

TEST_CASE("turnaround: a second boarding after the gap")
{
    Flight f;
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
    f.run(400);   // doors stay open: next boarding after the gap
    CHECK(f.count("BoardingWelcome") > welcomes);
}

// ---- cabin call buttons (VP_SPEC § 4.3a) ------------------------------------------------------------

TEST_CASE("call mode: open doors alone don't start boarding; a cabin call does")
{
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
    CHECK(f.count("BoardingComplete") == 1);
}

TEST_CASE("call mode: a call with the doors shut boards until the beacon")
{
    Flight f;
    f.set.boardingStart = static_cast<int>(BoardingStart::Call);
    f.run(5);
    f.purser.cabinCall();
    f.run(300);
    CHECK(f.count("BoardingWelcome") >= 1);
    CHECK(f.count("BoardingComplete") == 0);   // the doors never opened, so their being shut doesn't end boarding
    f.run(20, [](SimState& x) { x.beacon = true; });
    CHECK(f.count("BoardingComplete") == 1);
    CHECK(f.count("ArmDoors") == 1);
}

TEST_CASE("call mode: no AC power, the call is ignored")
{
    Flight f;
    f.set.boardingStart = static_cast<int>(BoardingStart::Call);
    f.s.acPower = false;
    f.run(5);
    f.purser.cabinCall();
    f.run(1);   // the call is handled on this tick, still without power
    f.run(120, [](SimState& x) { x.acPower = true; });
    CHECK(f.out.played.empty());
    CHECK(f.purser.stageLabel() == "At the gate");
}

TEST_CASE("call mode: Advance starts boarding")
{
    Flight f;
    f.set.boardingStart = static_cast<int>(BoardingStart::Call);
    f.run(5);
    f.purser.advance();
    f.run(60);
    CHECK(f.purser.stageLabel() == "Boarding");
}

TEST_CASE("a cabin call during the safety briefing makes the cabin check quick (Fenix expedite)")
{
    Flight f;
    f.set.expediteSeconds = 10;
    f.run(5);
    f.run(20, [](SimState& x) { x.beacon = true, x.enginesRunning = 2; });
    REQUIRE(f.runUntil([&] { return f.count("SafetyBriefing") == 1; }, 200));
    const double briefingStart = f.t;
    f.purser.cabinCall();
    f.run(1);
    // SafetyBriefing lasts 150 s in the fake player; the expedited check then takes at most 10 s (+ chain gap).
    REQUIRE(f.runUntil([&] { return f.count("CrewTakeSeats(takeoff)") == 1; }, 400));
    CHECK(f.t - briefingStart < 150 + 10 + 4);
    CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Played);   // the briefing audio is not cut
}

TEST_CASE("a cabin call during the cabin check before takeoff expedites it")
{
    Flight f;
    f.out.durations[AnnType::SafetyBriefing] = 20;
    f.run(5);
    f.run(20, [](SimState& x) { x.beacon = true, x.enginesRunning = 2; });
    REQUIRE(f.runUntil([&] { return f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Played; }, 300));
    f.run(5);   // the check (about 2 minutes for 150 pax on an A321) has started
    REQUIRE(f.count("CrewTakeSeats(takeoff)") == 0);
    f.purser.cabinCall();
    CHECK(f.runUntil([&] { return f.count("CrewTakeSeats(takeoff)") == 1; }, 20));
}

TEST_CASE("a cabin call in the descent expedites the cabin check before landing")
{
    Flight f;
    f.s.onGround = false;
    f.s.aglFt = 30000;
    f.s.mslFt = 30000;
    f.phase(Phase::Cruise);
    f.run(10);
    f.phase(Phase::Descent);
    f.run(5, [](SimState& x) { x.vsFpm = -2000; });
    f.purser.cabinCall();   // before DescentStarted: the check after it will be quick
    REQUIRE(f.runUntil([&] { return f.lastOutcome(AnnType::DescentStarted) == Outcome::Played; }, 200));
    f.run(1, [](SimState& x) { x.aglFt = 9000; });   // below the ready height
    CHECK(f.runUntil([&] { return f.count("CrewTakeSeats(landing)") == 1; }, 25));
}

TEST_CASE("a cabin call with nothing to do changes nothing")
{
    Flight f;
    f.s.onGround = false;
    f.s.aglFt = 30000;
    f.phase(Phase::Cruise);
    f.run(10);
    f.purser.cabinCall();
    f.run(10);
    CHECK(f.out.played.empty());
}

// ---- SimBrief boarding schedule (VP_SPEC § 4.1a) -----------------------------------------------------

namespace {

BoardingPlan planAt(double boardingAtSec, int seqno = 1)
{
    BoardingPlan p;
    p.valid = true;
    p.seqno = seqno;
    p.boardingAtSec = boardingAtSec;
    p.etdSec = boardingAtSec + 35 * 60;
    p.summary = "test plan";
    return p;
}

void scheduleMode(Flight& f, double zulu)
{
    f.set.boardingStart = static_cast<int>(BoardingStart::Schedule);
    f.s.zuluSec = zulu;
}

} // namespace

TEST_CASE("schedule: a plan seen before the boarding time waits silently, then boards on time")
{
    Flight f;
    scheduleMode(f, 12 * 3600);
    f.purser.setBoardingPlan(planAt(12 * 3600 + 600));
    f.run(5);
    f.run(500, [](SimState& x) { x.doorMax = 1, x.doorFwd = 1; });   // ToLiss AUTO door open long before boarding
    CHECK(f.out.played.empty());
    CHECK(f.out.music.empty());
    CHECK(f.purser.nextLabel().find("Boarding at 12:10Z") != std::string::npos);
    f.run(150);
    CHECK(f.purser.stageLabel() == "Boarding");
    CHECK(f.count("BoardingWelcome") >= 1);
    REQUIRE(!f.out.music.empty());
    CHECK(f.out.music.back().second);
    f.run(20, [](SimState& x) { x.doorMax = 0, x.doorFwd = 0; });   // doors close: boarding complete
    CHECK(f.count("BoardingComplete") == 1);
    CHECK(f.count("ArmDoors") == 0);                                 // Fenix: ArmDoors waits for the beacon
    f.run(20, [](SimState& x) { x.beacon = true; });
    CHECK(f.count("ArmDoors") == 1);
}

TEST_CASE("schedule: the boarding time comes with the doors shut: boarding starts, ends on the beacon")
{
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

TEST_CASE("schedule: a late load uses the doors rule")
{
    Flight f;
    scheduleMode(f, 12 * 3600 + 300);   // the boarding time was 5 min ago
    f.purser.setBoardingPlan(planAt(12 * 3600));
    f.run(60);
    CHECK(f.purser.stageLabel() == "At the gate");
    CHECK(f.purser.nextLabel().find("door has been open") != std::string::npos);
    f.run(10, [](SimState& x) { x.doorMax = 1; });
    CHECK(f.purser.stageLabel() == "At the gate");   // not yet 15 s
    f.run(10);
    CHECK(f.purser.stageLabel() == "Boarding");
}

TEST_CASE("schedule: without a valid plan the doors rule applies")
{
    Flight f;
    scheduleMode(f, 12 * 3600);
    f.purser.setBoardingPlan(BoardingPlan{});
    f.run(30);
    CHECK(f.out.played.empty());
    f.run(20, [](SimState& x) { x.doorMax = 1; });
    CHECK(f.purser.stageLabel() == "Boarding");

    Flight g;   // a plan but no zulu clock: doors rule too
    g.set.boardingStart = static_cast<int>(BoardingStart::Schedule);
    g.purser.setBoardingPlan(planAt(12 * 3600));
    g.run(5);
    g.run(20, [](SimState& x) { x.doorMax = 1; });
    CHECK(g.purser.stageLabel() == "Boarding");
}

TEST_CASE("schedule: a cabin call before the boarding time starts boarding")
{
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

TEST_CASE("schedule: a plan re-timed into the past while waiting starts boarding now")
{
    Flight f;
    scheduleMode(f, 12 * 3600);
    f.purser.setBoardingPlan(planAt(12 * 3600 + 1800));
    f.run(60, [](SimState& x) { x.doorMax = 1; });
    CHECK(f.purser.stageLabel() == "At the gate");
    f.purser.setBoardingPlan(planAt(12 * 3600 + 30, 2));   // a new OFP with an earlier ETD
    f.run(5);
    CHECK(f.purser.stageLabel() == "Boarding");
}

// ---- ToLiss cross-checks and the no-FMGS estimate (VP_SPEC § 4.1, § 4.2, D-20) ------------------------

TEST_CASE("takeoff thrust from the thrust levers when the FMGS stays at PreFlight")
{
    Flight f;
    f.run(5);
    f.run(30, [](SimState& x) { x.doorMax = 1; });
    f.run(60);
    f.run(20, [](SimState& x) { x.doorMax = 0, x.beacon = true, x.enginesRunning = 2; });
    f.run(75);
    REQUIRE(f.count("SafetyBriefing") == 1);
    f.run(5, [](SimState& x) { x.gsKt = 8, x.throttleMax = 0.88f; });   // FLX, rolling, APPhase still 0
    CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Stopped);
}

TEST_CASE("takeoff thrust from the ECAM flight phase")
{
    Flight f;
    f.run(5);
    f.run(30, [](SimState& x) { x.doorMax = 1; });
    f.run(60);
    f.run(20, [](SimState& x) { x.doorMax = 0, x.beacon = true, x.enginesRunning = 2; });
    f.run(75);
    REQUIRE(f.count("SafetyBriefing") == 1);
    f.run(2, [](SimState& x) { x.ecamPhase = 3; });   // T.O. power, not yet rolling
    CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Stopped);
}

TEST_CASE("thrust levers below the FLX detent are not takeoff thrust")
{
    Flight f;
    f.run(5);
    f.run(30, [](SimState& x) { x.doorMax = 1; });
    f.run(60);
    f.run(20, [](SimState& x) { x.doorMax = 0, x.beacon = true, x.enginesRunning = 2; });
    f.run(75);
    REQUIRE(f.count("SafetyBriefing") == 1);
    f.run(10, [](SimState& x) { x.gsKt = 15, x.throttleMax = 0.3f; });   // taxi thrust
    CHECK(f.lastOutcome(AnnType::SafetyBriefing) == Outcome::Playing);
}

TEST_CASE("no FMGS: an estimated phase still gives AfterTakeoff, CabinService and DescentStarted")
{
    Flight f;
    f.run(5);
    f.boardAndDepart();
    f.run(5, [](SimState& x) { x.cabOn = true; });
    f.run(5, [](SimState& x) { x.cabOn = false; });
    // APPhase stays PreFlight (0) the whole flight; QPAC and altitude drive the estimate.
    f.run(30, [](SimState& x) { x.gsKt = 140, x.throttleMax = 0.88f, x.ecamPhase = 4; });
    f.run(20, [](SimState& x) { x.onGround = false, x.aglFt = 800, x.vsFpm = 2500, x.ecamPhase = 5, x.qpacPhase = 6; });
    CHECK(!f.purser.phaseEstimated());   // not yet 30 s after lift-off
    f.run(200, [](SimState& x) { x.aglFt = 8000, x.mslFt = 8000, x.qpacPhase = 8, x.throttleMax = 0.7f; });
    CHECK(f.purser.phaseEstimated());
    CHECK(f.count("AfterTakeoff") == 1);
    f.run(300, [](SimState& x) { x.aglFt = 30000, x.mslFt = 30000; });
    f.run(900, [](SimState& x) { x.vsFpm = 0, x.aglFt = 34000, x.mslFt = 34000; });   // level, no cruise altitude
    CHECK(f.purser.phase() == Phase::Cruise);
    CHECK(f.count("CabinService") == 1);
    f.run(300, [](SimState& x) { x.vsFpm = -2000, x.aglFt = 20000, x.mslFt = 20000; });
    CHECK(f.purser.phase() == Phase::Descent);
    CHECK(f.count("DescentStarted") == 1);
    f.run(400, [](SimState& x) { x.aglFt = 3000, x.mslFt = 3000, x.qpacPhase = 9; });
    CHECK(f.purser.phase() == Phase::Approach);
    CHECK(f.count("CrewTakeSeats(landing)") == 1);
}

TEST_CASE("touchdown from the ECAM flight phase drops the landing calls")
{
    Flight f;
    f.s.onGround = false;
    f.s.aglFt = 9000;
    f.s.mslFt = 9000;
    f.phase(Phase::Descent);
    f.run(150, [](SimState& x) { x.vsFpm = -1000; });
    f.phase(Phase::Approach);
    f.run(5, [](SimState& x) { x.aglFt = 20; });
    f.run(5, [](SimState& x) { x.ecamPhase = 8; });   // the gear dataref hasn't said "on ground" yet
    f.run(150, [](SimState& x) { x.onGround = true, x.aglFt = 0, x.gsKt = 20, x.ecamPhase = 9; });
    CHECK(f.count("AfterLanding") == 1);
}

// ---- recorded flights (local only: runs/ is never committed) ------------------------------------------

TEST_CASE("replay of recorded traces in ../runs (local only)")
{
    namespace fs = std::filesystem;
    const fs::path runs = fs::path(FXA_SOURCE_DIR) / ".." / "runs";
    std::error_code ec;
    if (!fs::is_directory(runs, ec)) {
        MESSAGE("no runs/ folder: skipped");
        return;
    }
    int traces = 0;
    for (const auto& e : fs::recursive_directory_iterator(runs, ec)) {
        if (!e.is_regular_file() || e.path().string().find(".fxtrace.csv") == std::string::npos) continue;
        std::ifstream in(e.path(), std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::vector<TraceRow> rows = readTrace(ss.str());
        if (rows.empty()) continue;
        ++traces;
        // What the purser did in the sim, in order (one entry per type change).
        std::vector<std::string> recorded;
        bool calls = false;
        for (const TraceRow& r : rows) {
            if (!r.isEvent) continue;
            if (r.event.rfind("call button ", 0) == 0 && r.event.find("/mech") == std::string::npos) calls = true;
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
        set.boardingStart = static_cast<int>(calls ? BoardingStart::Call : BoardingStart::Doors);
        const SimState& first = rows.front().state;
        const Family fam = familyFrom(first.acfIcao, "");
        p.setAircraft(makeAircraftType(fam == Family::Unknown ? Family::A320 : fam,
                                       first.acfIcao == "A20N" || first.acfIcao == "A21N"));
        double last = -1;
        for (const TraceRow& r : rows) {
            if (r.isEvent) {
                if (r.event.rfind("call button ", 0) == 0 && r.event.find("/mech") == std::string::npos) p.cabinCall();
                continue;
            }
            // Recorded at 1 Hz; tick at 4 Hz in between so the purser's timers run as in the sim.
            SimState s = r.state;
            const double from = last < 0 ? s.t : last;
            for (double t = from + 0.25; t <= s.t + 1e-6; t += 0.25) {
                clock = t;
                s.t = t;
                p.tick(s, set);
            }
            last = r.state.t;
        }
        std::vector<std::string> replayed;
        for (const std::string& label : out.played) {
            const std::string type = label.substr(0, label.find_first_of("(#"));
            if (replayed.empty() || replayed.back() != type) replayed.push_back(type);
        }
        // Every recorded announcement appears in the replay, in the same order.
        size_t k = 0;
        for (const std::string& t : replayed)
            if (k < recorded.size() && t == recorded[k]) ++k;
        const std::string what = e.path().string() + ": replay has " + std::to_string(k) + " of " +
                                 std::to_string(recorded.size()) + " recorded announcements in order";
        CHECK_MESSAGE(k == recorded.size(), what);
    }
    const std::string done = std::to_string(traces) + " recorded traces replayed";
    MESSAGE(done);
}
