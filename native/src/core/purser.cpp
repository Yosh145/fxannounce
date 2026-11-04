#include "core/purser.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/log.h"

namespace fxa {

namespace {

constexpr size_t kTimelineRows = 300;
constexpr double kSeatbeltDescentWindow =
    180;  // vp_spec 4.1 no separate seatbeltsignon around descent

bool atLeast(Phase p, Phase q) {
  return static_cast<int>(p) >= static_cast<int>(q);
}

std::string mmss(double s) {
  if (s < 0) s = 0;
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d:%02d", static_cast<int>(s) / 60,
                static_cast<int>(s) % 60);
  return buf;
}

std::string hmm(double s) {
  if (s < 0) s = 0;
  const int m = static_cast<int>(std::lround(s / 60.0));
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d:%02d h", m / 60, m % 60);
  return buf;
}

constexpr double kBoardingJitter = 0.15;  // 4.1b
constexpr double kBoardingMaxFactor = 1.5;
constexpr double kRetimeMinS = 60;
constexpr double kClockJumpS = 300;
constexpr double kLateStartS = 60;  // late start threshold
constexpr double kSettleS = 10;     // vp_spec 4.4
constexpr double kTaxiKt = 5;       // taxiing 4.2

}  // namespace

const char* outcomeName(Outcome o) {
  switch (o) {
    case Outcome::Queued:
      return "queued";
    case Outcome::Playing:
      return "playing";
    case Outcome::Played:
      return "played";
    case Outcome::Skipped:
      return "skipped";
    case Outcome::NoSound:
      return "no sound in pack";
    case Outcome::Expired:
      return "expired";
    case Outcome::Disabled:
      return "disabled";
    case Outcome::Stopped:
      return "stopped";
    case Outcome::Unanswered:
      return "call unanswered";
  }
  return "?";
}

Purser::Purser(PurserOutput& out, unsigned seed) : out_(out), rng_(seed) {}

double Purser::randIn(double lo, double hi) {
  if (hi < lo) std::swap(lo, hi);
  std::uniform_real_distribution<double> d(lo, hi);
  return d(rng_);
}

double Purser::checkDuration(int pax, const Settings& set) {
  // vp_spec 4.1 d12 45s 0.35s/pax x type factor x 1 plus minus 20 pct x
  // multiplier
  return (45.0 + 0.35 * std::max(0, pax)) * type_.typeFactor *
         randIn(0.8, 1.2) * std::max(0.1f, set.checkMultiplier);
}

void Purser::reset() {
  if (current_ && !call_.active) out_.stop();
  queue_.clear();
  current_.reset();
  call_ = Call{};
  f_ = Flags{};
  gapUntil_ = 0;
  callPressed_ = false;
  boardingNote_.clear();
  lastZulu_ = lastZuluT_ = -1;
  if (music_) out_.setMusic(false);
  music_ = false;
}

void Purser::note(double t, const std::string& text) {
  logInfo("purser: %s", text.c_str());
  if (noteSink_) noteSink_(t, text);
}

void Purser::log(double t, AnnType type, CallContext c, Outcome o,
                 std::string detail) {
  TimelineRow r{t, type, c, o, std::move(detail)};
  auto clock = [&](double base) {
    return base < 0 ? -1.0
                    : std::fmod(std::fmod(base + (t - now_), 86400.0) + 86400.0,
                                86400.0);
  };
  r.zuluSec = clock(zulu_);
  r.localSec = clock(local_);
  timeline_.push_back(r);
  while (timeline_.size() > kTimelineRows) timeline_.pop_front();
  logInfo("purser: %s%s%s%s %s%s", annTypeName(type),
          c != CallContext::Any ? " (" : "",
          c != CallContext::Any ? contextName(c) : "",
          c != CallContext::Any ? ")" : "", outcomeName(o),
          r.detail.empty() ? "" : (": " + r.detail).c_str());
  if (sink_) sink_(r);
}

void Purser::enqueue(AnnType t, CallContext c, int expire, double notBefore) {
  queue_.push_back(Item{t, c, expire, notBefore});
}

bool Purser::expired(const Item& it, const SimState& s,
                     const Settings& set) const {
  if ((it.expire & ExpireAtTakeoff) && f_.takeoff) return true;
  if ((it.expire & ExpireOnGround) && s.onGround && f_.flown) return true;
  if ((it.expire & ExpireAirborne) && !s.onGround) return true;
  if ((it.expire & ExpireLandingWindow) &&
      (s.onGround || s.aglFt < set.landingDropAglFt || phase_ == Phase::GA))
    return true;
  return false;
}

void Purser::dropWhere(int mask, Outcome why) {
  for (auto it = queue_.begin(); it != queue_.end();) {
    if (it->expire & mask) {
      log(now_, it->type, it->ctx, why);
      onFinished(*it);
      it = queue_.erase(it);
    } else {
      ++it;
    }
  }
}

void Purser::onFinished(const Item& it) {
  switch (it.type) {
    case AnnType::ArmDoors:
      f_.armDoorsFinished = true;
      f_.armDoorsAt = now_;
      break;
    case AnnType::SafetyBriefing:
      f_.safetyFinished = true;
      break;
    case AnnType::DescentStarted:
      f_.descentFinished = true;
      break;
    default:
      break;
  }
}

void Purser::finishCurrent(Outcome o, std::string detail) {
  if (!current_) return;
  for (auto r = timeline_.rbegin(); r != timeline_.rend(); ++r) {
    if (r->type == current_->type && r->context == current_->ctx &&
        (r->outcome == Outcome::Playing || r->outcome == Outcome::Queued)) {
      r->outcome = o;
      if (!detail.empty()) r->detail = detail;
      if (sink_) sink_(*r);
      break;
    }
  }
  const Item done = *current_;
  current_.reset();
  call_ = Call{};
  onFinished(done);
}

// phase

Phase Purser::estimatePhase(const SimState& s) {
  // no fmgs or never initialized 4.2 d20
  if (s.onGround) {
    f_.levelSince = -1;
    if (!f_.flown)
      return (s.enginesRunning > 0 && s.gsKt > 40) ? Phase::Takeoff
                                                   : Phase::PreFlight;
    return Phase::Done;
  }
  // qpac phase first liftoff 6 approach 9-10 7-8 refined below
  const bool qpac = s.qpacPhase >= 1;
  if (s.qpacPhase == 6) return Phase::Takeoff;
  if (s.qpacPhase == 9 || s.qpacPhase == 10) return Phase::Approach;
  if (!qpac && !f_.climbEntered && s.aglFt < 1500 && s.vsFpm > 0)
    return Phase::Takeoff;
  // level fmgs cruise alt or above 10000 ft for 60s if there isnt one
  const bool level = std::fabs(s.vsFpm) < 300;
  if (level) {
    if (f_.levelSince < 0) f_.levelSince = s.t;
  } else {
    f_.levelSince = -1;
  }
  if (s.cruiseAltFt > 1000 && std::fabs(s.mslFt - s.cruiseAltFt) < 1000 &&
      std::fabs(s.vsFpm) < 500)
    return Phase::Cruise;
  if (s.cruiseAltFt <= 1000 && level && s.mslFt > 10000 && f_.levelSince >= 0 &&
      s.t - f_.levelSince >= 60)
    return Phase::Cruise;
  if (s.vsFpm < -500) return s.aglFt < 4000 ? Phase::Approach : Phase::Descent;
  if (s.vsFpm > 300) return Phase::Climb;
  return phase_ == Phase::PreFlight || phase_ == Phase::Done ||
                 phase_ == Phase::Takeoff
             ? Phase::Climb
             : phase_;
}

bool Purser::estimateWanted(const SimState& s, const Settings& set) const {
  if (!s.fmgsPhase) return true;
  // uninitialized fmgs sits at preflight all flight toliss also drops done to
  // preflight at shutdown run2 which doesnt count
  return *s.fmgsPhase == Phase::PreFlight && !f_.fmgsLeftPreflight &&
         f_.flown && f_.liftoffAt >= 0 &&
         s.t - f_.liftoffAt >= set.noFmgsAfterS;
}

// mid session start

void Purser::infer(const SimState& s) {
  f_.inferred = true;
  f_.beltState = s.seatbelt;
  f_.slidesWere = s.slidesArmed;
  const bool airborne = !s.onGround && s.aglFt > 50;
  const Phase p =
      s.fmgsPhase ? *s.fmgsPhase : (airborne ? Phase::Climb : Phase::PreFlight);
  if (airborne) {
    f_.boardingComplete = f_.armDoors = f_.armDoorsFinished = f_.safetyQueued =
        f_.safetyFinished = true;
    f_.takeoffSecuredQueued = f_.lineup = f_.taxied = true;
    f_.takeoffCallsQueued = f_.takeoff = f_.flown = true;
    f_.ecamTakeoffSeen = true;
    f_.liftoffAt = s.t;
    f_.takeoffCheckDone = 0;
    f_.climbEntered = true;
    f_.afterTakeoff = atLeast(p, Phase::Cruise) || s.aglFt > 5000;
    if (atLeast(p, Phase::Cruise)) f_.cruiseEntered = true;
    if (atLeast(p, Phase::Descent)) {
      f_.cabinService = true;
      f_.descentEntered = f_.descentStarted = f_.descentFinished = true;
      f_.descentAt = s.t - 600;
    }
    if (atLeast(p, Phase::Approach)) {
      f_.landingCheckDone = 0;
      f_.landingSecuredQueued = f_.seatsLanding = f_.landingCallsQueued = true;
    }
    logInfo("purser: started airborne (%s): earlier events marked done",
            phaseName(p));
  } else if (s.gsKt > 1) {
    f_.boardingComplete = f_.armDoors = f_.armDoorsFinished = true;
    f_.armDoorsAt = f_.movedAt = s.t;
    f_.safetyQueued = f_.safetyFinished = f_.takeoffSecuredQueued = f_.lineup =
        f_.takeoffCallsQueued = true;
    f_.takeoffCheckDone = 0;
    f_.taxied = s.enginesRunning > 0;
    logInfo(
        "purser: started moving on the ground: boarding, doors and the safety "
        "briefing marked done");
  } else if (s.enginesRunning > 0) {
    // hot start no boarding w engines running shutting down at the gate starts
    // over taxiing out runs departure from here
    f_.boardingComplete = f_.armDoors = f_.armDoorsFinished = true;
    f_.armDoorsAt = s.t;
    f_.hotStart = true;
    boardingNote_ =
        "Started with engines running: boarding skipped. Shut the engines down "
        "at the gate to board.";
    note(s.t,
         "started parked with engines running: boarding skipped until the "
         "engines are shut down at the gate");
  }
  prevPhase_ = phase_ = p;
}

// departure

void Purser::departure(const SimState& s, const Settings& set) {
  if (f_.takeoff) return;
  // takeoff thrust 4.1 fmgs takeoff phase rolling levers flx/toga rolling ecam
  // to power/80kt or engines on above 40 kt
  const char* why = nullptr;
  if (s.onGround) {
    const bool rolling = s.gsKt > 5;
    if (s.fmgsPhase && *s.fmgsPhase == Phase::Takeoff && rolling)
      why = "FMGS takeoff phase";
    else if (s.throttleKnown() && s.throttleMax >= set.throttleFlxDetent &&
             rolling && s.enginesRunning > 0)
      why = "thrust levers at FLX/TOGA";
    else if (s.ecamPhase == 3 || s.ecamPhase == 4)
      why = "ECAM flight phase T.O. power";
    else if (s.enginesRunning > 0 && s.gsKt > 40)
      why = "engines running above 40 kt";
  } else if (f_.flown) {
    why = "airborne";
  }
  if (why) {
    f_.takeoff = true;
    boardingNote_.clear();
    note(s.t, std::string("takeoff thrust (") + why + ")");
    // d6
    if (current_) {
      if (call_.active && !call_.talking) {
        finishCurrent(Outcome::Expired, "takeoff");
      } else {
        out_.stop();
        finishCurrent(Outcome::Stopped, "takeoff thrust");
      }
    }
    dropWhere(ExpireAtTakeoff, Outcome::Expired);
    // d23 no lineup signal came
    if (f_.takeoffSecuredQueued && !f_.takeoffCallsQueued)
      log(s.t, AnnType::CrewTakeSeats, CallContext::Takeoff, Outcome::Expired,
          "no line-up signal before takeoff thrust");
    f_.boardingComplete = f_.armDoors = true;
    return;
  }

  const bool atGate = s.onGround && s.enginesRunning == 0 && s.gsKt < 1;
  // hot start 4.4
  if (f_.hotStart && !f_.flown && atGate && (s.parkBrake || s.chocks)) {
    freshFlight(s);
    note(s.t, "engines shut down at the gate: boarding can start");
    return;
  }
  // turnaround gap 4.4 ends on time or door close reopen
  if (f_.turnaroundAt >= 0) {
    if (s.doorMax < 0.05f) {
      f_.gapSawClosed = true;
    } else if (f_.gapSawClosed && s.doorMax > 0.5f) {
      f_.turnaroundAt = -1;
      note(s.t,
           "a door opened again after the turnaround: the turnaround gap ends");
    }
  }
  if (atGate && s.doorMax > 0.5f) f_.doorSeenOpenAtGate = true;

  // boarding start 4.1a d18
  if (!f_.boarding && !f_.boardingComplete && !f_.armDoors) {
    switch (mode_) {
      case BoardingStart::Schedule:
        scheduleBoarding(s, set, atGate);
        break;
      case BoardingStart::Doors:
        doorsBoarding(s, set, atGate);
        break;
      case BoardingStart::Call:
        break;
    }
  }
  if (f_.boarding && !f_.boardingComplete) {
    // a new ofp during boarding re-times its end 4.1b
    if (mode_ == BoardingStart::Schedule && plan_.valid &&
        plan_.seqno != f_.boardingPlanSeqno) {
      f_.boardingPlanSeqno = plan_.seqno;
      f_.boardingEndAt = std::max(s.t + kRetimeMinS, boardingEnd(s, set));
      note(s.t, "new SimBrief plan during boarding: boarding completes in " +
                    mmss(f_.boardingEndAt - s.t));
    }
    if (s.t >= f_.nextWelcome) {
      const bool already =
          std::any_of(queue_.begin(), queue_.end(),
                      [](const Item& i) {
                        return i.type == AnnType::BoardingWelcome;
                      }) ||
          (current_ && current_->type == AnnType::BoardingWelcome);
      if (!already)
        enqueue(AnnType::BoardingWelcome, CallContext::Any, ExpireAtTakeoff);
      f_.nextWelcome = s.t + randIn(set.welcomeMin, set.welcomeMax);
    }
    // d1 door events never complete boarding toliss auto doors close on their
    // own timer
    const char* why = nullptr;
    if (f_.forceBoardingComplete)
      why = "advanced";
    else if (s.t >= f_.boardingEndAt)
      why = "boarding time up";
    else if (s.beacon)
      why = "beacon on";
    else if (s.gsKt > 1)
      why = "pushback";
    else if (s.enginesRunning > 0)
      why = "engine running";
    else if (s.slidesKnown && s.slidesArmed && !f_.slidesWere)
      why = "slides armed";
    if (why) {
      note(s.t, std::string("boarding complete (") + why + ")");
      if (!f_.clockMismatch) boardingNote_.clear();
      f_.boardingComplete = true;
      f_.forceBoardingComplete = false;
      queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                  [](const Item& i) {
                                    return i.type == AnnType::BoardingWelcome;
                                  }),
                   queue_.end());
      enqueue(AnnType::BoardingComplete, CallContext::Any, ExpireAtTakeoff);
    }
  }

  // armdoors on beacon push engine start or slides armed
  if (!f_.armDoors) {
    const bool slidesRising = s.slidesKnown && s.slidesArmed && !f_.slidesWere;
    if (s.beacon || s.gsKt > 1 || s.enginesRunning > 0 || slidesRising) {
      if (!f_.boarding) f_.boardingComplete = true;
      if (f_.armWaitSince < 0) f_.armWaitSince = s.t;
      if (s.doorMax < 0.05f || s.t - f_.armWaitSince >= set.armDoorsWait ||
          s.gsKt > 1) {
        f_.armDoors = true;
        enqueue(AnnType::ArmDoors, CallContext::Any, ExpireAtTakeoff);
      }
    }
  }

  // d22
  if (f_.boardingComplete && f_.movedAt < 0 && s.onGround && s.gsKt > 1) {
    f_.movedAt = s.t;
    f_.safetyAfterMove =
        randIn(set.preSafetyAfterMoveMin, set.preSafetyAfterMoveMax);
    logInfo(
        "purser: the aircraft is moving: the safety briefing follows in about "
        "%.0f s",
        f_.safetyAfterMove);
  }
  if (f_.armDoors && !f_.taxied && s.onGround && s.enginesRunning > 0 &&
      s.gsKt > kTaxiKt) {
    f_.taxied = true;
    logInfo("purser: taxiing (a cabin call is now the line-up signal)");
  }

  if (f_.armDoorsFinished && !f_.safetyQueued && f_.movedAt >= 0 &&
      s.t >= safetyDue()) {
    f_.safetyQueued = true;
    enqueue(AnnType::PreSafetyBriefing, CallContext::Any, ExpireAtTakeoff);
    enqueue(AnnType::SafetyBriefing, CallContext::Any, ExpireAtTakeoff);
  }

  // d23 lights are level triggered cabin call once taxied is the 3rd one
  // oncabincall
  if (f_.armDoors && !f_.lineup && !f_.takeoffCallsQueued && s.onGround) {
    if (s.landingLights)
      signalLineup(s, set, "landing lights ON");
    else if (s.strobeOn == 1)
      signalLineup(s, set, "strobe ON");
  }

  // briefing to check to cabinsecured while taxiing to lineup to seats dim
  if (f_.safetyFinished && f_.takeoffCheckDone < 0) {
    const double d = checkDuration(s.pax, set);
    f_.takeoffCheckDone = s.t + (f_.expediteTakeoffCheck
                                     ? std::min<double>(d, set.expediteSeconds)
                                     : d);
  }
  if (f_.takeoffCheckDone >= 0 && !f_.takeoffSecuredQueued &&
      s.t >= f_.takeoffCheckDone) {
    f_.takeoffSecuredQueued = true;
    enqueue(AnnType::CabinSecured, CallContext::Takeoff, ExpireAtTakeoff);
  }
  if (f_.takeoffSecuredQueued && f_.lineup && !f_.takeoffCallsQueued) {
    f_.takeoffCallsQueued = true;
    enqueue(AnnType::CrewTakeSeats, CallContext::Takeoff, ExpireAtTakeoff);
    if (dark(s, set))
      enqueue(AnnType::CabinDim, CallContext::Takeoff, ExpireAtTakeoff);
  }
}

void Purser::signalLineup(const SimState& s, const Settings& set,
                          const std::string& why) {
  f_.lineup = true;
  std::string text = "line-up signal (" + why + ")";
  // early signal hurries the check d23
  if (!f_.takeoffSecuredQueued) {
    f_.expediteTakeoffCheck = true;
    if (f_.takeoffCheckDone >= 0)
      f_.takeoffCheckDone =
          std::min<double>(f_.takeoffCheckDone, s.t + set.expediteSeconds);
    text += ": the cabin check is hurried";
  }
  note(s.t, text);
}

void Purser::signalSeatsLanding(const SimState& s, const Settings& set,
                                const std::string& why) {
  f_.seatsLanding = true;
  std::string text = "seats-for-landing signal (" + why + ")";
  if (!f_.landingSecuredQueued) {
    f_.expediteLandingCheck = true;
    if (f_.landingCheckDone >= 0)
      f_.landingCheckDone =
          std::min<double>(f_.landingCheckDone, s.t + set.expediteSeconds);
    text += ": the cabin check is hurried";
  }
  note(s.t, text);
}

void Purser::signalAfterTakeoff(const SimState& s, const std::string& why) {
  // d25
  f_.climbEntered = true;
  f_.afterTakeoffDue = s.t;
  note(s.t, "crew may leave their seats (" + why + "): AfterTakeoff now");
}

bool Purser::detectFlick(const SimState& s, const Settings& set) {
  if (s.noSmoking < 0) {
    f_.nsLast = -1;
    f_.nsOffAt = -1;
    return false;
  }
  bool flick = false;
  if (f_.nsLast == 1 && s.noSmoking == 0) {
    f_.nsOffAt = s.t;
  } else if (f_.nsLast == 0 && s.noSmoking == 1) {
    flick = f_.nsOffAt >= 0 && s.t - f_.nsOffAt <= set.nsFlickMaxS;
    f_.nsOffAt = -1;
  }
  f_.nsLast = s.noSmoking;
  return flick;
}

void Purser::freshFlight(const SimState& s) {
  Flags fresh;
  fresh.inferred = true;
  fresh.beltState = s.seatbelt;
  fresh.slidesWere = s.slidesArmed;
  fresh.nsLast = s.noSmoking;
  f_ = fresh;
  boardingNote_.clear();
}

void Purser::scheduleBoarding(const SimState& s, const Settings& set,
                              bool atGate) {
  const bool usable = plan_.valid && s.zuluSec >= 0;
  if (!usable) {
    if (f_.clockMismatch) {
      f_.clockMismatch = false;
      boardingNote_.clear();
    }
    if (f_.sched != Sched::Doors || f_.planSeqno != -2) {
      logInfo(
          "purser: no valid SimBrief plan: boarding starts when a door has "
          "been open %.0f s",
          static_cast<double>(set.boardingDoorSettle));
      f_.planSeqno = -2;
      f_.planAt = -1;
    }
    f_.sched = Sched::Doors;
    doorsBoarding(s, set, atGate);
    return;
  }
  if (plan_.seqno != f_.planSeqno || plan_.boardingAtSec != f_.planAt) {
    // not seen this plan yet before its time we wait late load doors rule
    // retimed plan while waiting keeps waiting so a time that slid into the
    // past boards now
    f_.planSeqno = plan_.seqno;
    f_.planAt = plan_.boardingAtSec;
    const double until = secondsUntil(plan_.boardingAtSec, s.zuluSec);
    if (f_.clockMismatch) boardingNote_.clear();
    f_.clockMismatch = until > set.maxWaitMin * 60.0;
    if (f_.clockMismatch) {
      // xp clock not on flight time 4.1a doors rule but ofp still picks the
      // pack
      f_.sched = Sched::Doors;
      boardingNote_ = "SimBrief boarding time is " + hmm(until) +
                      " away: is the X-Plane clock set to the flight's time? "
                      "Using the doors rule.";
      note(s.t, boardingNote_);
      doorsBoarding(s, set, atGate);
      return;
    }
    f_.sched = (until > 0 || f_.sched == Sched::Waiting) ? Sched::Waiting
                                                         : Sched::Doors;
    if (f_.sched == Sched::Waiting) {
      note(s.t, "boarding at " + zuluLabel(plan_.boardingAtSec) +
                    " (SimBrief, in " + mmss(until) + ")");
    } else {
      char buf[160];
      std::snprintf(buf, sizeof(buf),
                    "the SimBrief boarding time %s has passed: boarding starts "
                    "when a door has been open %.0f s",
                    zuluLabel(plan_.boardingAtSec).c_str(),
                    static_cast<double>(set.boardingDoorSettle));
      note(s.t, buf);
    }
  }
  if (f_.sched == Sched::Waiting) {
    if (secondsUntil(plan_.boardingAtSec, s.zuluSec) > 0) return;
    if (atGate) {
      f_.sched = Sched::Unknown;
      startBoarding(s, set, "SimBrief boarding time");
      return;
    }
    note(s.t, "SimBrief boarding time reached away from the gate: doors rule");
    f_.sched = Sched::Doors;
  }
  doorsBoarding(s, set, atGate);
}

void Purser::doorsBoarding(const SimState& s, const Settings& set,
                           bool atGate) {
  // d18 ac power after any gap door open or was open at this gate auto door can
  // close before power for boarding_door_settle_s
  const bool gapOk = f_.turnaroundAt < 0 || s.t >= f_.turnaroundAt;
  if (!atGate || !s.acPower || !f_.doorSeenOpenAtGate || !gapOk) {
    f_.readySince = -1;
    return;
  }
  if (f_.readySince < 0) f_.readySince = s.t;
  if (s.t - f_.readySince >= set.boardingDoorSettle)
    startBoarding(s, set,
                  s.doorMax > 0.5f
                      ? "doors open"
                      : "AC power on, a door was open at this gate");
}

double Purser::boardingEnd(const SimState& s, const Settings& set) {
  // 4.1b plan only counts in schedule mode w a sane clock
  const double minLen = std::max(30.0f, set.boardingMinS);
  const double natural = std::max(minLen, f_.boardingNatural);
  const bool clockOff = f_.clockMismatch;
  if (!clockOff) boardingNote_.clear();
  if (mode_ != BoardingStart::Schedule || !plan_.valid || clockOff ||
      s.zuluSec < 0)
    return f_.boardingStartAt + natural;
  const double toEtd = secondsUntil(plan_.etdSec, s.zuluSec);
  const double len = (s.t - f_.boardingStartAt) + toEtd - set.endBeforeEtdS;
  // on schedule follows the schedule only a late start under the natural length
  // warns obt left at its 30 min default etc
  const bool late =
      f_.boardingStartZulu >= 0 &&
      secondsUntil(plan_.boardingAtSec, f_.boardingStartZulu) < -kLateStartS;
  if (len >= natural || !late)
    return f_.boardingStartAt +
           std::clamp(len, minLen, kBoardingMaxFactor * natural);
  const double shortened = std::max(minLen, len);
  const int minutes = static_cast<int>(std::lround(shortened / 60.0));
  char buf[220];
  if (toEtd <= 0)
    std::snprintf(buf, sizeof(buf),
                  "Boarding shortened to %d min: the SimBrief ETD %s has "
                  "passed. Set the OBT later in "
                  "SimBrief for a full boarding.",
                  minutes, zuluLabel(plan_.etdSec).c_str());
  else
    std::snprintf(buf, sizeof(buf),
                  "Boarding shortened to %d min: the SimBrief ETD is only %d "
                  "min away. Set the OBT later "
                  "in SimBrief for a full boarding.",
                  minutes, static_cast<int>(toEtd / 60.0));
  boardingNote_ = buf;
  note(s.t, buf);
  return f_.boardingStartAt + shortened;
}

void Purser::startBoarding(const SimState& s, const Settings& set,
                           const char* why) {
  f_.boarding = true;
  f_.nextWelcome = s.t + randIn(20, 45);
  f_.turnaroundAt = -1;
  // pax from toliss else the plan
  const int pax = s.pax > 0 ? s.pax : std::max(0, plan_.pax);
  f_.boardingNatural = (set.boardingBaseS + set.boardingPerPaxS * pax) *
                       type_.typeFactor *
                       randIn(1.0 - kBoardingJitter, 1.0 + kBoardingJitter);
  f_.boardingStartAt = s.t;
  f_.boardingStartZulu = s.zuluSec;
  f_.boardingPlanSeqno = plan_.valid ? plan_.seqno : -1;
  f_.boardingEndAt = boardingEnd(s, set);
  note(s.t, std::string("boarding started (") + why + "), complete in " +
                mmss(f_.boardingEndAt - s.t));
}

void Purser::watchClock(const SimState& s) {
  // 4.1a clock jump before boarding
  if (s.zuluSec < 0) {
    lastZulu_ = lastZuluT_ = -1;
    return;
  }
  if (lastZulu_ >= 0 && lastZuluT_ >= 0) {
    const double expected = lastZulu_ + (s.t - lastZuluT_);
    const double jump = std::fabs(secondsUntil(s.zuluSec, expected));
    if (jump > kClockJumpS && !f_.boarding && !f_.boardingComplete &&
        !f_.armDoors && !f_.takeoff && f_.planSeqno >= 0) {
      note(s.t, "the X-Plane clock jumped by " + mmss(jump) +
                    ": the SimBrief plan is evaluated again");
      f_.planSeqno = -1;
      f_.planAt = -1;
    }
  }
  lastZulu_ = s.zuluSec;
  lastZuluT_ = s.t;
}

void Purser::onCabinCall(const SimState& s, const Settings& set) {
  // vp_spec 4.3a
  const bool atGate = s.onGround && s.enginesRunning == 0 && s.gsKt < 1;
  if (!f_.takeoff && !f_.boarding && !f_.boardingComplete && !f_.armDoors &&
      atGate) {
    if (!s.acPower) {
      logInfo("purser: cabin call ignored: no AC power in the cabin");
      return;
    }
    startBoarding(s, set, "cabin call");
    return;
  }
  if (f_.hotStart && f_.movedAt < 0 && s.onGround) {
    logInfo(
        "purser: cabin call: started with engines running; shut the engines "
        "down at the gate to board");
    return;
  }
  if (!f_.takeoff && f_.boarding && !f_.boardingComplete) {
    // d21
    const double by = s.t + set.boardingExpediteS;
    if (by < f_.boardingEndAt) {
      f_.boardingEndAt = by;
      note(s.t, "cabin call: boarding completes in " + mmss(by - s.t));
    } else {
      logInfo("purser: cabin call: boarding completes in %s anyway",
              mmss(f_.boardingEndAt - s.t).c_str());
    }
    return;
  }
  const double quick = s.t + set.expediteSeconds;
  if (!f_.takeoff && f_.armDoors && !f_.takeoffCallsQueued) {
    // taxied lineup signal d23 before that it just hurries the check fenix
    if (f_.taxied) {
      if (!f_.lineup)
        signalLineup(s, set, "cabin call while taxiing");
      else
        logInfo("purser: cabin call: the line-up signal was already given");
      return;
    }
    if (f_.takeoffSecuredQueued) {
      logInfo(
          "purser: cabin call: the cabin is secured; a call once taxiing is "
          "the line-up signal");
    } else if (f_.takeoffCheckDone >= 0) {
      f_.takeoffCheckDone = std::min(f_.takeoffCheckDone, quick);
      logInfo("purser: cabin call: cabin check before takeoff expedited");
    } else {
      f_.expediteTakeoffCheck = true;
      logInfo(
          "purser: cabin call: the cabin check after the safety briefing will "
          "be quick");
    }
    return;
  }
  if (f_.flown && !s.onGround && !f_.afterTakeoff && !f_.descentEntered) {
    signalAfterTakeoff(s, "cabin call");
    return;
  }
  if (f_.flown && f_.descentEntered && !f_.landingSecuredQueued) {
    if (f_.landingCheckDone >= 0) {
      f_.landingCheckDone = std::min(f_.landingCheckDone, quick);
      logInfo("purser: cabin call: cabin check before landing expedited");
    } else {
      f_.expediteLandingCheck = true;
      logInfo(
          "purser: cabin call: the cabin check before landing will be quick");
    }
    return;
  }
  logInfo("purser: cabin call: nothing to start or expedite now");
}

// in flight

void Purser::flight(const SimState& s, const Settings& set) {
  if (!f_.takeoff) return;
  const bool airborne = !s.onGround;

  // d25 in the descent its the seats signal instead
  const bool climbFlick =
      flick_ && airborne && f_.flown && !f_.afterTakeoff && !f_.descentEntered;
  if (climbFlick) signalAfterTakeoff(s, "NO SMOKING flick");
  if (!f_.climbEntered && airborne &&
      (phase_ == Phase::Climb || phase_ == Phase::Cruise ||
       (estimating_ && s.aglFt > 1500))) {
    f_.climbEntered = true;
    f_.afterTakeoffDue = s.t + randIn(set.afterTakeoffMin, set.afterTakeoffMax);
  }
  if (f_.afterTakeoffDue >= 0 && !f_.afterTakeoff &&
      s.t >= f_.afterTakeoffDue) {
    f_.afterTakeoff = true;
    enqueue(AnnType::AfterTakeoff, CallContext::Any, ExpireOnGround);
  }

  if (airborne && f_.flown) {
    if (s.seatbelt != f_.beltState) {
      if (!f_.beltPending) {
        f_.beltPending = true;
        f_.beltChangedAt = s.t;
      }
      if (s.t - f_.beltChangedAt >= set.seatbeltDebounce) {
        f_.beltState = s.seatbelt;
        f_.beltPending = false;
        const double after = f_.afterTakeoff ? 0
                                             : (f_.afterTakeoffDue >= 0
                                                    ? f_.afterTakeoffDue + 0.1
                                                    : s.t + 60);
        if (s.seatbelt) {
          const bool nearDescent =
              atLeast(phase_, Phase::Descent) ||
              (f_.descentAt >= 0 &&
               std::fabs(s.t - f_.descentAt) < kSeatbeltDescentWindow);
          if (!nearDescent)
            enqueue(AnnType::SeatbeltSignOn, CallContext::Any, ExpireOnGround,
                    after);
          else
            logDebug(
                "purser: seatbelt sign on near the descent: covered by "
                "DescentStarted");
        } else {
          enqueue(AnnType::SeatbeltSignOff, CallContext::Any, ExpireOnGround,
                  after);
        }
      }
    } else {
      f_.beltPending = false;
    }
  } else {
    f_.beltState = s.seatbelt;
    f_.beltPending = false;
  }

  if (!f_.cruiseEntered && airborne && phase_ == Phase::Cruise) {
    f_.cruiseEntered = true;
    f_.cabinServiceDue = s.t + randIn(set.cabinServiceMin, set.cabinServiceMax);
  }
  if (f_.cabinServiceDue >= 0 && !f_.cabinService &&
      s.t >= f_.cabinServiceDue) {
    f_.cabinService = true;
    if (phase_ == Phase::Cruise && !f_.decompression)
      enqueue(AnnType::CabinService, CallContext::Any, ExpireOnGround);
    else
      log(s.t, AnnType::CabinService, CallContext::Any, Outcome::Skipped,
          "no longer in cruise");
  }

  if (!f_.descentEntered && airborne &&
      (phase_ == Phase::Descent || phase_ == Phase::Approach)) {
    f_.descentEntered = true;
    f_.descentAt = s.t;
    f_.descentDue = s.t + randIn(set.descentMin, set.descentMax);
    if (!f_.cabinService && f_.cabinServiceDue >= 0) {
      f_.cabinService = true;
      log(s.t, AnnType::CabinService, CallContext::Any, Outcome::Skipped,
          "descent started first");
    }
  }
  if (f_.descentDue >= 0 && !f_.descentStarted && s.t >= f_.descentDue) {
    f_.descentStarted = true;
    enqueue(AnnType::DescentStarted, CallContext::Any, ExpireOnGround);
  }

  if (f_.descentFinished && f_.landingCheckDone < 0) {
    const double d = checkDuration(s.pax, set);
    f_.landingCheckDone = s.t + (f_.expediteLandingCheck
                                     ? std::min<double>(d, set.expediteSeconds)
                                     : d);
  }
  // d24
  const bool window =
      airborne &&
      (phase_ == Phase::Approach || s.aglFt < set.landingReadyAglFt ||
       f_.forceLandingCalls) &&
      s.aglFt > set.landingDropAglFt;
  if (f_.landingCheckDone >= 0 && !f_.landingSecuredQueued &&
      s.t >= f_.landingCheckDone && window) {
    f_.landingSecuredQueued = true;
    f_.forceLandingCalls = false;
    enqueue(AnnType::CabinSecured, CallContext::Landing, ExpireLandingWindow);
  }
  if (f_.descentEntered && airborne && !f_.seatsLanding &&
      !f_.landingCallsQueued) {
    if (flick_)
      signalSeatsLanding(s, set, "NO SMOKING flick");
    else if (s.gearLeverDown == 1)
      signalSeatsLanding(s, set, "gear down");
  } else if (flick_ && !climbFlick && airborne && f_.flown &&
             !f_.descentEntered) {
    logInfo("purser: NO SMOKING flick in cruise: nothing to signal");
  }
  if (f_.landingSecuredQueued && f_.seatsLanding && !f_.landingCallsQueued &&
      airborne && s.aglFt > set.landingDropAglFt) {
    f_.landingCallsQueued = true;
    enqueue(AnnType::CrewTakeSeats, CallContext::Landing, ExpireLandingWindow);
    if (dark(s, set))
      enqueue(AnnType::CabinDim, CallContext::Landing, ExpireLandingWindow);
  }

  if (phase_ == Phase::GA && prevPhase_ != Phase::GA) {
    logInfo("purser: go-around: pending landing calls dropped");
    dropWhere(ExpireLandingWindow, Outcome::Expired);
    // nothing repeats 4.1
    if (f_.landingSecuredQueued) f_.landingCallsQueued = true;
  }
}

// arrival

void Purser::arrival(const SimState& s, const Settings& set) {
  if (!f_.flown) return;
  // touchdown 4.1
  const bool ecamDown = s.ecamPhase == 8 || s.ecamPhase == 9;
  const bool down = s.onGround || (ecamDown && s.aglFt < 50);
  if (!down && s.aglFt > 50 && f_.touchdownAt >= 0 && !f_.afterLanding)
    f_.touchdownAt = -1;  // touch-and-go
  if (down && f_.touchdownAt < 0 && !f_.afterLanding) {
    f_.touchdownAt = s.t;
    note(s.t, std::string("touchdown (") +
                  (s.onGround ? "on ground" : "ECAM flight phase") + ")");
    dropWhere(ExpireOnGround | ExpireLandingWindow, Outcome::Expired);
  }
  if (f_.touchdownAt < 0 || !down) return;

  if (!f_.afterLanding) {
    const bool stowed = s.speedbrake <= 0.01f && s.groundSpoilers != 1;
    const bool normal = stowed && !s.landingLights && s.gsKt < 40;
    const bool fallback =
        s.t - f_.touchdownAt >= set.afterLandingFallback && s.gsKt < 30;
    if (normal || fallback) {
      f_.afterLanding = true;
      enqueue(AnnType::AfterLanding);
    }
  }
  if (!f_.disarm) {
    const bool slidesFalling = s.slidesKnown && !s.slidesArmed && f_.slidesWere;
    const bool atStand =
        s.gsKt < 1 && (s.parkBrake || s.chocks) && (!s.beacon || !s.seatbelt);
    if (atStand || slidesFalling) {
      if (!f_.afterLanding) {
        f_.afterLanding = true;
        enqueue(AnnType::AfterLanding);
      }
      f_.disarm = true;
      f_.disarmAt = s.t;
      enqueue(AnnType::DisarmDoors);
    }
  }
  if (f_.disarm && !f_.deboarding &&
      (s.doorFwd > 0.5f || s.doorMax > 0.5f ||
       s.t - f_.disarmAt >= set.deboardFallback)) {
    f_.deboarding = true;
    f_.deboardAt = s.t;
    f_.deboardDone =
        s.t + std::max<double>(set.deboardMin,
                               s.pax * set.deboardPerPax * type_.typeFactor);
    enqueue(AnnType::DeboardingStarted);
  }
  if (f_.deboarding && !f_.deboardComplete && s.t >= f_.deboardDone) {
    f_.deboardComplete = true;
    enqueue(AnnType::DeboardingComplete);
  }
  if (f_.deboardComplete && !f_.flightComplete && queue_.empty() && !current_) {
    if (!set.turnaround) {
      // d26
      f_.flightComplete = true;
      boardingNote_.clear();
      note(s.t,
           "flight complete: a new SimBrief OFP from this airport starts the "
           "next flight (Turnaround is off)");
      return;
    }
    // turnaround a new flight boarding restarts after the gap or when the doors
    // close and reopen
    logInfo(
        "purser: turnaround, next boarding in %.0f s or when a door closes and "
        "opens again",
        static_cast<double>(set.turnaroundGap));
    freshFlight(s);
    f_.turnaroundAt = s.t + set.turnaroundGap;
  }
}

// queue interphone

void Purser::runCall(const SimState& s, const Settings& set) {
  const Item it = call_.item;
  if (call_.talking) {
    if (skipRequested_) {
      out_.stop();
      finishCurrent(Outcome::Stopped);
      gapUntil_ = s.t + set.chainGap;
    } else if (!out_.playing()) {
      finishCurrent(Outcome::Played);
      gapUntil_ = s.t + set.chainGap;
    }
    return;
  }
  if (expired(it, s, set)) {
    finishCurrent(Outcome::Expired);
    return;
  }
  if (skipRequested_) {
    finishCurrent(Outcome::Stopped, "call cancelled");
    return;
  }
  if (s.cabOn || call_.forceAnswer) {
    const auto label = out_.play(it.type, it.ctx, true);
    if (!label) {
      finishCurrent(Outcome::NoSound);
      return;
    }
    call_.talking = true;
    for (auto r = timeline_.rbegin(); r != timeline_.rend(); ++r)
      if (r->type == it.type && r->outcome == Outcome::Queued) {
        r->outcome = Outcome::Playing;
        r->detail =
            *label + (call_.forceAnswer ? " (advanced)" : " (CAB answered)");
        if (sink_) sink_(*r);
        break;
      }
    return;
  }
  if (s.t >= call_.nextChime) {
    if (++call_.attempt > set.interphoneRetries) {
      finishCurrent(Outcome::Unanswered,
                    "no answer after " + std::to_string(set.interphoneRetries) +
                        " retries");
      return;
    }
    out_.chime();
    call_.nextChime = s.t + set.interphoneRetry;
  }
}

void Purser::runQueue(const SimState& s, const Settings& set) {
  if (current_) {
    if (call_.active) {
      runCall(s, set);
      skipRequested_ = false;
      return;
    }
    if (skipRequested_) {
      out_.stop();
      finishCurrent(Outcome::Stopped, "skipped");
      gapUntil_ = s.t;
    } else if (!out_.playing()) {
      finishCurrent(Outcome::Played);
      gapUntil_ = s.t + set.chainGap;
    } else {
      return;
    }
  }
  skipRequested_ = false;

  for (auto it = queue_.begin(); it != queue_.end();) {
    if (expired(*it, s, set)) {
      log(s.t, it->type, it->ctx, Outcome::Expired);
      onFinished(*it);
      it = queue_.erase(it);
    } else {
      ++it;
    }
  }
  if (!s.acPower || s.replay || s.t < gapUntil_) return;  // d13

  while (true) {
    auto ready = std::find_if(queue_.begin(), queue_.end(), [&](const Item& i) {
      return i.notBefore <= s.t;
    });
    if (ready == queue_.end()) return;
    const Item it = *ready;
    queue_.erase(ready);
    if (!set.eventEnabled(annTypeName(it.type))) {
      log(s.t, it.type, it.ctx, Outcome::Disabled);
      onFinished(it);
      continue;
    }
    const std::optional<Mode> mode = out_.peek(it.type, it.ctx);
    if (!mode) {
      log(s.t, it.type, it.ctx, Outcome::NoSound);
      onFinished(it);
      continue;
    }
    if (*mode == Mode::Interphone) {
      // 4.3 buzzer then wait for cab
      current_ = it;
      call_ = Call{};
      call_.active = true;
      call_.item = it;
      call_.attempt = 0;
      call_.nextChime = s.t + set.interphoneRetry;
      out_.chime();
      log(s.t, it.type, it.ctx, Outcome::Queued,
          "cabin calling: pull CAB on the ACP");
      return;
    }
    const auto label = out_.play(it.type, it.ctx, false);
    if (!label) {
      log(s.t, it.type, it.ctx, Outcome::NoSound);
      onFinished(it);
      continue;
    }
    current_ = it;
    log(s.t, it.type, it.ctx, Outcome::Playing, *label);
    return;
  }
}

// tick advance labels

void Purser::updateMusic(const SimState& s) {
  const bool want = s.acPower && !s.replay &&
                    ((f_.boarding && !f_.boardingComplete) ||
                     (f_.deboarding && !f_.deboardComplete));
  if (want != music_) {
    music_ = want;
    out_.setMusic(want);
  }
}

void Purser::applyAdvance(const SimState& s, const Settings& set) {
  const double t = s.t;
  gapUntil_ = t;
  if (call_.active && !call_.talking) {
    call_.forceAnswer = true;
    return;
  }
  double* best = nullptr;
  double bestAt = 1e300;
  auto consider = [&](double& at, bool live) {
    if (live && at > t && at < bestAt) {
      bestAt = at;
      best = &at;
    }
  };
  double preSafetyAt = f_.movedAt >= 0 ? safetyDue() : -1;
  consider(preSafetyAt,
           f_.armDoorsFinished && !f_.safetyQueued && f_.movedAt >= 0);
  consider(f_.takeoffCheckDone,
           !f_.takeoffSecuredQueued && f_.takeoffCheckDone >= 0);
  consider(f_.afterTakeoffDue, !f_.afterTakeoff && f_.afterTakeoffDue >= 0);
  consider(f_.cabinServiceDue, !f_.cabinService && f_.cabinServiceDue >= 0);
  consider(f_.descentDue, !f_.descentStarted && f_.descentDue >= 0);
  consider(f_.landingCheckDone,
           !f_.landingSecuredQueued && f_.landingCheckDone >= 0);
  consider(f_.deboardDone, f_.deboarding && !f_.deboardComplete);
  consider(f_.turnaroundAt, f_.turnaroundAt >= 0);
  for (Item& i : queue_) consider(i.notBefore, true);
  if (best) {
    if (best == &preSafetyAt) {
      f_.movedAt = std::min(f_.movedAt, t - f_.safetyAfterMove);
      f_.armDoorsAt =
          std::min(f_.armDoorsAt, t - static_cast<double>(set.preSafetyDelay));
    } else {
      *best = t;
    }
    return;
  }
  // waiting on the aircraft/pilots not a timer advance stands in
  if (f_.armDoorsFinished && !f_.safetyQueued && f_.movedAt < 0 &&
      !f_.takeoff) {
    f_.movedAt = t - f_.safetyAfterMove;
    f_.armDoorsAt =
        std::min(f_.armDoorsAt, t - static_cast<double>(set.preSafetyDelay));
    return;
  }
  if (f_.takeoffSecuredQueued && !f_.lineup && !f_.takeoffCallsQueued &&
      !f_.takeoff) {
    signalLineup(s, set, "advanced");
    return;
  }
  if (!f_.boarding && !f_.boardingComplete && !f_.armDoors && !f_.takeoff &&
      s.onGround && s.acPower) {
    startBoarding(s, set, "advanced");
    return;
  }
  if (f_.boarding && !f_.boardingComplete) {
    f_.forceBoardingComplete = true;
    return;
  }
  if (f_.landingCheckDone >= 0 && !f_.landingSecuredQueued) {
    f_.forceLandingCalls = true;
    return;
  }
  if (f_.landingSecuredQueued && !f_.seatsLanding && !f_.landingCallsQueued) {
    signalSeatsLanding(s, set, "advanced");
    return;
  }
  if (f_.disarm && !f_.deboarding) {
    f_.disarmAt = t - set.deboardFallback;
    return;
  }
  logInfo("purser: nothing to advance");
}

void Purser::tick(const SimState& s, const Settings& set) {
  now_ = s.t;
  zulu_ = s.zuluSec;
  local_ = s.localTimeSec;
  preSafetyDelay_ = set.preSafetyDelay;
  doorSettle_ = set.boardingDoorSettle;
  mode_ = set.boarding();
  if (!f_.inferred) {
    // xp still settling right after load run2 0.7 kt first tick so take the
    // start state ksettles later 4.4
    if (f_.settleFrom < 0) f_.settleFrom = s.t;
    if (s.t - f_.settleFrom < kSettleS) return;
    infer(s);
  }
  watchClock(s);
  flick_ = detectFlick(s, set);
  if (s.fmgsPhase && *s.fmgsPhase != Phase::PreFlight)
    f_.fmgsLeftPreflight = true;
  // d26
  if (!f_.takeoff && !f_.flown && plan_.valid) f_.flightSeqno = plan_.seqno;
  if (f_.flightComplete && plan_.valid && plan_.seqno != f_.flightSeqno &&
      s.onGround) {
    const int seqno = plan_.seqno;
    freshFlight(s);
    f_.flightSeqno = seqno;
    note(s.t, "new SimBrief OFP for this airport: the next flight begins");
  }

  // liftoff 4.2 5s above 50 ft agl or ecam liftoff/climb after its takeoff
  // phases
  if (s.ecamPhase == 3 || s.ecamPhase == 4) f_.ecamTakeoffSeen = true;
  if (!s.onGround && s.aglFt > 50) {
    if (f_.airborneSince < 0) f_.airborneSince = s.t;
    if (s.t - f_.airborneSince >= 5 && !f_.flown) {
      f_.flown = true;
      f_.liftoffAt = f_.airborneSince;
    }
  } else if (s.onGround) {
    f_.airborneSince = -1;
  }
  if (!f_.flown && f_.ecamTakeoffSeen && s.ecamPhase >= 5 && s.ecamPhase <= 7 &&
      !s.onGround) {
    f_.flown = true;
    f_.liftoffAt = s.t;
    note(s.t,
         "lift-off (ECAM flight phase " + std::to_string(s.ecamPhase) + ")");
  }

  // d20
  const bool wantEstimate = estimateWanted(s, set);
  if (wantEstimate && !estimating_ && !f_.estimateLogged && s.fmgsPhase) {
    f_.estimateLogged = true;
    char buf[128];
    std::snprintf(buf, sizeof(buf),
                  "the FMGS phase is still PreFlight %.0f s after lift-off "
                  "(FMGS not initialized): estimating the phase",
                  static_cast<double>(set.noFmgsAfterS));
    note(s.t, buf);
  }
  estimating_ = wantEstimate;
  phase_ = estimating_ ? estimatePhase(s) : *s.fmgsPhase;

  if (!f_.decompression && s.paxMasks && s.mslFt > 10000) {
    // 4.1
    f_.decompression = true;
    if (current_) {
      out_.stop();
      finishCurrent(Outcome::Stopped, "decompression");
    }
    for (const Item& i : queue_)
      log(s.t, i.type, i.ctx, Outcome::Expired, "decompression");
    queue_.clear();
    f_.cabinService = true;
    queue_.push_front(
        Item{AnnType::Decompression, CallContext::Any, ExpireOnGround, 0});
    gapUntil_ = s.t;
  }

  if (advanceRequested_) {
    advanceRequested_ = false;
    applyAdvance(s, set);
  }
  if (callPressed_) {
    callPressed_ = false;
    onCabinCall(s, set);
  }
  departure(s, set);
  flight(s, set);
  arrival(s, set);
  updateMusic(s);
  runQueue(s, set);
  f_.slidesWere = s.slidesArmed;
  prevPhase_ = phase_;
}

void Purser::skip() { skipRequested_ = true; }
void Purser::advance() { advanceRequested_ = true; }

std::string Purser::stageLabel() const {
  if (f_.decompression && f_.touchdownAt < 0) return "Decompression";
  if (f_.flightComplete) return "Flight complete";
  if (f_.deboarding)
    return f_.deboardComplete ? "Deboarding complete" : "Deboarding";
  if (f_.disarm) return "At the stand";
  if (f_.touchdownAt >= 0) return "Landed, taxiing in";
  if (f_.flown || f_.takeoff) {
    switch (phase_) {
      case Phase::Takeoff:
        return "Takeoff";
      case Phase::Climb:
        return "Climb";
      case Phase::Cruise:
        return "Cruise";
      case Phase::Descent:
        return "Descent";
      case Phase::Approach:
        return f_.landingCallsQueued     ? "Approach, crew seated"
               : f_.landingSecuredQueued ? "Approach, cabin secured"
                                         : "Approach";
      case Phase::GA:
        return "Go-around";
      default:
        return "Airborne";
    }
  }
  if (f_.takeoffCallsQueued) return "Crew seated for takeoff";
  if (f_.takeoffSecuredQueued) return "Cabin secured, waiting for line-up";
  if (f_.safetyFinished) return "Cabin check before takeoff";
  if (f_.safetyQueued) return "Safety briefing";
  if (f_.armDoors) return "Doors armed";
  if (f_.boardingComplete) return "Boarding complete";
  if (f_.boarding) return "Boarding";
  if (f_.turnaroundAt >= 0) return "Turnaround";
  return "At the gate";
}

std::vector<PendingItem> Purser::pending() const {
  std::vector<std::pair<double, PendingItem>> v;
  const double t = now_;
  auto timed = [&](double at, PendingItem it) {
    it.inSec = std::max(0.0, at - t);
    v.emplace_back(at, std::move(it));
  };
  auto waits = [&](double rank, PendingItem it) {
    it.inSec = -1;
    v.emplace_back(t + rank, std::move(it));
  };
  auto ann = [](AnnType a, CallContext c = CallContext::Any,
                std::string note = {}, std::string cond = {}) {
    PendingItem it;
    it.type = a;
    it.context = c;
    it.note = std::move(note);
    it.condition = std::move(cond);
    return it;
  };
  auto milestone = [](std::string what, std::string cond = {}) {
    PendingItem it;
    it.what = std::move(what);
    it.condition = std::move(cond);
    return it;
  };
  for (const Item& i : queue_)
    timed(std::max(i.notBefore, t), ann(i.type, i.ctx));
  if (f_.boarding && !f_.boardingComplete) {
    timed(f_.nextWelcome, ann(AnnType::BoardingWelcome));
    timed(f_.boardingEndAt, ann(AnnType::BoardingComplete));
    waits(1e6, ann(AnnType::BoardingComplete, CallContext::Any, {},
                   "sooner on beacon, pushback, engine start or a cabin call"));
  }
  if (!f_.armDoors && f_.boardingComplete)
    waits(1e6, ann(AnnType::ArmDoors, CallContext::Any, {},
                   "on beacon, pushback or engine start"));
  if (f_.hotStart)
    waits(2e6,
          milestone("Boarding", "after the engines are shut down at the gate"));
  if (f_.flightComplete)
    waits(2e6, milestone("Next flight",
                         "with a new SimBrief OFP from this airport (or switch "
                         "on Settings > Announcements > Turnaround)"));
  if (!f_.boarding && !f_.boardingComplete && !f_.armDoors && !f_.takeoff) {
    if (mode_ == BoardingStart::Schedule && f_.sched == Sched::Waiting &&
        zulu_ >= 0) {
      const double until = secondsUntil(plan_.boardingAtSec, zulu_);
      timed(t + std::max(0.0, until),
            milestone("Boarding at " + zuluLabel(plan_.boardingAtSec) +
                      " (SimBrief)"));
    } else if (mode_ == BoardingStart::Call) {
      waits(2e6,
            milestone(
                "Boarding",
                "starts on a cabin call (overhead CALLS: FWD, AFT, ALL...)"));
    } else {
      char buf[128];
      std::snprintf(buf, sizeof(buf),
                    "starts %.0f s after a door is open at the gate with "
                    "power, or on a cabin call",
                    static_cast<double>(doorSettle_));
      waits(2e6, milestone("Boarding", buf));
    }
  }
  if (f_.armDoorsFinished && !f_.safetyQueued && !f_.takeoff) {
    if (f_.movedAt >= 0)
      timed(safetyDue(), ann(AnnType::PreSafetyBriefing));
    else
      waits(1e6, ann(AnnType::PreSafetyBriefing, CallContext::Any, {},
                     "when the aircraft moves (pushback or taxi)"));
  }
  if (f_.takeoffCheckDone >= 0 && !f_.takeoffSecuredQueued)
    timed(f_.takeoffCheckDone, ann(AnnType::CabinSecured, CallContext::Takeoff,
                                   "when the cabin check ends"));
  if (f_.takeoffSecuredQueued && !f_.takeoffCallsQueued && !f_.takeoff)
    waits(1e6, ann(AnnType::CrewTakeSeats, CallContext::Takeoff, {},
                   f_.lineup ? "after the cabin call"
                             : "on line-up: landing lights ON, strobe ON, or a "
                               "cabin call while taxiing"));
  if (f_.afterTakeoffDue >= 0 && !f_.afterTakeoff)
    timed(f_.afterTakeoffDue, ann(AnnType::AfterTakeoff));
  if (f_.cabinServiceDue >= 0 && !f_.cabinService)
    timed(f_.cabinServiceDue, ann(AnnType::CabinService));
  if (f_.descentDue >= 0 && !f_.descentStarted)
    timed(f_.descentDue, ann(AnnType::DescentStarted));
  if (f_.landingCheckDone >= 0 && !f_.landingSecuredQueued)
    timed(f_.landingCheckDone, ann(AnnType::CabinSecured, CallContext::Landing,
                                   "when the cabin check ends, in the approach "
                                   "or below the ready height"));
  if (f_.landingSecuredQueued && !f_.landingCallsQueued && !f_.seatsLanding)
    waits(1e6, ann(AnnType::CrewTakeSeats, CallContext::Landing, {},
                   "on a NO SMOKING flick or the gear down"));
  if (f_.deboarding && !f_.deboardComplete)
    timed(f_.deboardDone, ann(AnnType::DeboardingComplete));
  if (f_.turnaroundAt >= 0 && !f_.boarding)
    timed(f_.turnaroundAt, milestone("Next boarding"));
  std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) {
    return a.first < b.first;
  });
  std::vector<PendingItem> out;
  out.reserve(v.size());
  for (auto& p : v) out.push_back(std::move(p.second));
  return out;
}

std::vector<std::string> Purser::pendingLabels() const {
  std::vector<std::string> out;
  for (const PendingItem& p : pending()) {
    std::string line = p.type ? annFenixLabel(*p.type, p.context) : p.what;
    if (!p.note.empty()) line += ", " + p.note;
    if (p.inSec >= 0)
      line += p.inSec > 0 ? " in " + mmss(p.inSec) : std::string(" now");
    else if (!p.condition.empty())
      line += " " + p.condition;
    out.push_back(std::move(line));
  }
  return out;
}

std::string Purser::nextLabel() const {
  const auto p = pendingLabels();
  return p.empty() ? std::string("-") : p.front();
}

std::string Purser::nowLabel() const {
  if (!current_) return "-";
  std::string s = annTypeName(current_->type);
  if (current_->ctx != CallContext::Any)
    s += std::string(" (") + contextName(current_->ctx) + ")";
  if (call_.active && !call_.talking)
    s += ": cabin calling, attempt " + std::to_string(call_.attempt + 1);
  return s;
}

}  // namespace fxa
