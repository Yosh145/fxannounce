#include "core/purser.h"

#include "core/log.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace fxa {

namespace {

constexpr size_t kTimelineRows = 300;
constexpr double kSeatbeltDescentWindow = 180;   // s around DescentStarted without a separate SeatbeltSignOn (VP_SPEC § 4.1)

bool atLeast(Phase p, Phase q) { return static_cast<int>(p) >= static_cast<int>(q); }

std::string mmss(double s)
{
    if (s < 0) s = 0;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d:%02d", static_cast<int>(s) / 60, static_cast<int>(s) % 60);
    return buf;
}

} // namespace

const char* outcomeName(Outcome o)
{
    switch (o) {
    case Outcome::Queued: return "queued";
    case Outcome::Playing: return "playing";
    case Outcome::Played: return "played";
    case Outcome::Skipped: return "skipped";
    case Outcome::NoSound: return "no sound in pack";
    case Outcome::Expired: return "expired";
    case Outcome::Disabled: return "disabled";
    case Outcome::Stopped: return "stopped";
    case Outcome::Unanswered: return "call unanswered";
    }
    return "?";
}

Purser::Purser(PurserOutput& out, unsigned seed) : out_(out), rng_(seed) {}

double Purser::randIn(double lo, double hi)
{
    if (hi < lo) std::swap(lo, hi);
    std::uniform_real_distribution<double> d(lo, hi);
    return d(rng_);
}

double Purser::checkDuration(int pax, const Settings& set)
{
    // VP_SPEC § 4.1 (D-12): (45 s + 0.35 s per passenger) x type factor x (1 +/- 20 %) x multiplier.
    return (45.0 + 0.35 * std::max(0, pax)) * type_.typeFactor * randIn(0.8, 1.2) * std::max(0.1f, set.checkMultiplier);
}

void Purser::reset()
{
    if (current_ && !call_.active) out_.stop();
    queue_.clear();
    current_.reset();
    call_ = Call{};
    f_ = Flags{};
    gapUntil_ = 0;
    callPressed_ = false;
    if (music_) out_.setMusic(false);
    music_ = false;
}

void Purser::note(double t, const std::string& text)
{
    logInfo("purser: %s", text.c_str());
    if (noteSink_) noteSink_(t, text);
}

void Purser::log(double t, AnnType type, CallContext c, Outcome o, std::string detail)
{
    TimelineRow r{ t, type, c, o, std::move(detail) };
    timeline_.push_back(r);
    while (timeline_.size() > kTimelineRows) timeline_.pop_front();
    logInfo("purser: %s%s%s %s%s%s", annTypeName(type), c != CallContext::Any ? " (" : "", c != CallContext::Any ? contextName(c) : "",
            c != CallContext::Any ? ") " : "", outcomeName(o), r.detail.empty() ? "" : (": " + r.detail).c_str());
    if (sink_) sink_(r);
}

void Purser::enqueue(AnnType t, CallContext c, int expire, double notBefore)
{
    queue_.push_back(Item{ t, c, expire, notBefore });
}

bool Purser::expired(const Item& it, const SimState& s, const Settings& set) const
{
    if ((it.expire & ExpireAtTakeoff) && f_.takeoff) return true;
    if ((it.expire & ExpireOnGround) && s.onGround && f_.flown) return true;
    if ((it.expire & ExpireAirborne) && !s.onGround) return true;
    if ((it.expire & ExpireLandingWindow) && (s.onGround || s.aglFt < set.landingDropAglFt || phase_ == Phase::GA)) return true;
    return false;
}

void Purser::dropWhere(int mask, Outcome why)
{
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

void Purser::onFinished(const Item& it)
{
    switch (it.type) {
    case AnnType::ArmDoors:
        f_.armDoorsFinished = true;
        f_.armDoorsAt = now_;
        break;
    case AnnType::SafetyBriefing: f_.safetyFinished = true; break;
    case AnnType::DescentStarted: f_.descentFinished = true; break;
    default: break;
    }
}

void Purser::finishCurrent(Outcome o, std::string detail)
{
    if (!current_) return;
    // Update the row of the announcement that was playing (or the call that was ringing).
    for (auto r = timeline_.rbegin(); r != timeline_.rend(); ++r) {
        if (r->type == current_->type && r->context == current_->ctx && (r->outcome == Outcome::Playing || r->outcome == Outcome::Queued)) {
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

// ---- phase -----------------------------------------------------------------------

Phase Purser::estimatePhase(const SimState& s)
{
    // Used when the FMGS phase is unavailable or the FMGS was never initialized (VP_SPEC § 4.2, D-20).
    if (s.onGround) {
        f_.levelSince = -1;
        if (!f_.flown) return (s.enginesRunning > 0 && s.gsKt > 40) ? Phase::Takeoff : Phase::PreFlight;
        return Phase::Done;
    }
    // ToLiss's QPAC flight phase first: lift-off (6), approach (9-10). 7-8 (1500 ft, flaps up) are refined below.
    const bool qpac = s.qpacPhase >= 1;
    if (s.qpacPhase == 6) return Phase::Takeoff;
    if (s.qpacPhase == 9 || s.qpacPhase == 10) return Phase::Approach;
    if (!qpac && !f_.climbEntered && s.aglFt < 1500 && s.vsFpm > 0) return Phase::Takeoff;
    // Level flight: at the FMGS cruise altitude, or (no cruise altitude) above 10,000 ft for 60 s.
    const bool level = std::fabs(s.vsFpm) < 300;
    if (level) {
        if (f_.levelSince < 0) f_.levelSince = s.t;
    } else {
        f_.levelSince = -1;
    }
    if (s.cruiseAltFt > 1000 && std::fabs(s.mslFt - s.cruiseAltFt) < 1000 && std::fabs(s.vsFpm) < 500) return Phase::Cruise;
    if (s.cruiseAltFt <= 1000 && level && s.mslFt > 10000 && f_.levelSince >= 0 && s.t - f_.levelSince >= 60)
        return Phase::Cruise;
    if (s.vsFpm < -500) return s.aglFt < 4000 ? Phase::Approach : Phase::Descent;
    if (s.vsFpm > 300) return Phase::Climb;
    return phase_ == Phase::PreFlight || phase_ == Phase::Done || phase_ == Phase::Takeoff ? Phase::Climb : phase_;
}

bool Purser::estimateWanted(const SimState& s, const Settings& set) const
{
    if (!s.fmgsPhase) return true;
    // An FMGS that was never initialized stays at PreFlight through the flight.
    return *s.fmgsPhase == Phase::PreFlight && f_.flown && f_.liftoffAt >= 0 && s.t - f_.liftoffAt >= set.noFmgsAfterS;
}

// ---- mid-session start ------------------------------------------------------------

void Purser::infer(const SimState& s)
{
    f_.inferred = true;
    f_.beltState = s.seatbelt;
    f_.slidesWere = s.slidesArmed;
    const bool airborne = !s.onGround && s.aglFt > 50;
    const Phase p = s.fmgsPhase ? *s.fmgsPhase : (airborne ? Phase::Climb : Phase::PreFlight);
    if (airborne) {
        f_.boardingComplete = f_.armDoors = f_.armDoorsFinished = f_.safetyQueued = f_.safetyFinished = true;
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
            f_.landingCallsQueued = true;
        }
        logInfo("purser: started airborne (%s): earlier events marked done", phaseName(p));
    } else if (s.enginesRunning > 0 || s.gsKt > 1) {
        f_.boardingComplete = f_.armDoors = f_.armDoorsFinished = true;
        f_.armDoorsAt = s.t;
        if (s.gsKt > 1) {
            f_.safetyQueued = f_.safetyFinished = f_.takeoffCallsQueued = true;
            f_.takeoffCheckDone = 0;
        }
        logInfo("purser: started with engines running%s: boarding and doors marked done", s.gsKt > 1 ? " and moving" : "");
    }
    prevPhase_ = phase_ = p;
}

// ---- departure ------------------------------------------------------------------

void Purser::departure(const SimState& s, const Settings& set)
{
    if (f_.takeoff) return;
    // Takeoff thrust (VP_SPEC § 4.1): the first of the FMGS takeoff phase while rolling, the thrust levers at FLX/TOGA
    // while rolling, the ECAM T.O. power or 80 kt phase, engines running above 40 kt.
    const char* why = nullptr;
    if (s.onGround) {
        const bool rolling = s.gsKt > 5;
        if (s.fmgsPhase && *s.fmgsPhase == Phase::Takeoff && rolling) why = "FMGS takeoff phase";
        else if (s.throttleKnown() && s.throttleMax >= set.throttleFlxDetent && rolling && s.enginesRunning > 0)
            why = "thrust levers at FLX/TOGA";
        else if (s.ecamPhase == 3 || s.ecamPhase == 4) why = "ECAM flight phase T.O. power";
        else if (s.enginesRunning > 0 && s.gsKt > 40) why = "engines running above 40 kt";
    } else if (f_.flown) {
        why = "airborne";
    }
    if (why) {
        f_.takeoff = true;
        note(s.t, std::string("takeoff thrust (") + why + ")");
        // D-6: takeoff thrust stops whatever plays; pre-takeoff items are dropped.
        if (current_) {
            if (call_.active && !call_.talking) {
                finishCurrent(Outcome::Expired, "takeoff");
            } else {
                out_.stop();
                finishCurrent(Outcome::Stopped, "takeoff thrust");
            }
        }
        dropWhere(ExpireAtTakeoff, Outcome::Expired);
        f_.boardingComplete = f_.armDoors = true;
        return;
    }

    const bool atGate = s.onGround && s.enginesRunning == 0 && s.gsKt < 1;
    if (f_.turnaroundAt >= 0 && s.doorMax < 0.05f) f_.turnaroundAt = -1;   // doors closed: the next opening is a new boarding

    // Boarding start (§ 4.1a, D-18): the SimBrief boarding time, the doors rule, or a cabin call (onCabinCall).
    if (!f_.boarding && !f_.boardingComplete && !f_.armDoors) {
        switch (mode_) {
        case BoardingStart::Schedule: scheduleBoarding(s, set, atGate); break;
        case BoardingStart::Doors: doorsBoarding(s, set, atGate); break;
        case BoardingStart::Call: break;
        }
    }
    if (f_.boarding && !f_.boardingComplete) {
        if (s.doorMax > 0.5f) f_.doorsSeenOpen = true;
        if (s.t >= f_.nextWelcome) {
            const bool already = std::any_of(queue_.begin(), queue_.end(), [](const Item& i) { return i.type == AnnType::BoardingWelcome; }) ||
                                 (current_ && current_->type == AnnType::BoardingWelcome);
            if (!already) enqueue(AnnType::BoardingWelcome, CallContext::Any, ExpireAtTakeoff);
            f_.nextWelcome = s.t + randIn(set.welcomeMin, set.welcomeMax);
        }
        // Doors closing completes boarding only if they were open during it (a call can start boarding with them shut).
        if (f_.doorsSeenOpen && s.doorMax < 0.05f) {
            if (f_.doorsClosedSince < 0) f_.doorsClosedSince = s.t;
        } else {
            f_.doorsClosedSince = -1;
        }
        const bool done = (f_.doorsClosedSince >= 0 && s.t - f_.doorsClosedSince >= set.boardingClosedHold) || s.beacon ||
                          s.gsKt > 1 || s.enginesRunning > 0 || f_.forceBoardingComplete;
        if (done) {
            f_.boardingComplete = true;
            f_.forceBoardingComplete = false;
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Item& i) { return i.type == AnnType::BoardingWelcome; }),
                         queue_.end());
            enqueue(AnnType::BoardingComplete, CallContext::Any, ExpireAtTakeoff);
        }
    }

    // ArmDoors: beacon, pushback, engine start, or slides armed by the user
    if (!f_.armDoors) {
        const bool slidesRising = s.slidesKnown && s.slidesArmed && !f_.slidesWere;
        if (s.beacon || s.gsKt > 1 || s.enginesRunning > 0 || slidesRising) {
            if (!f_.boarding) f_.boardingComplete = true;   // no boarding happened (doors never opened)
            if (f_.armWaitSince < 0) f_.armWaitSince = s.t;
            if (s.doorMax < 0.05f || s.t - f_.armWaitSince >= set.armDoorsWait || s.gsKt > 1) {
                f_.armDoors = true;
                enqueue(AnnType::ArmDoors, CallContext::Any, ExpireAtTakeoff);
            }
        }
    }

    // Safety briefing about a minute after the doors are armed
    if (f_.armDoorsFinished && !f_.safetyQueued && s.t - f_.armDoorsAt >= set.preSafetyDelay) {
        f_.safetyQueued = true;
        enqueue(AnnType::PreSafetyBriefing, CallContext::Any, ExpireAtTakeoff);
        enqueue(AnnType::SafetyBriefing, CallContext::Any, ExpireAtTakeoff);
    }

    // Cabin check, then crew take seats, cabin dim (dark), cabin secured
    if (f_.safetyFinished && f_.takeoffCheckDone < 0) {
        const double d = checkDuration(s.pax, set);
        f_.takeoffCheckDone = s.t + (f_.expediteTakeoffCheck ? std::min<double>(d, set.expediteSeconds) : d);
    }
    if (f_.takeoffCheckDone >= 0 && !f_.takeoffCallsQueued && s.t >= f_.takeoffCheckDone) {
        f_.takeoffCallsQueued = true;
        enqueue(AnnType::CrewTakeSeats, CallContext::Takeoff, ExpireAtTakeoff);
        if (dark(s, set)) enqueue(AnnType::CabinDim, CallContext::Takeoff, ExpireAtTakeoff);
        enqueue(AnnType::CabinSecured, CallContext::Takeoff, ExpireAtTakeoff);
    }
}

void Purser::scheduleBoarding(const SimState& s, const Settings& set, bool atGate)
{
    const bool usable = plan_.valid && s.zuluSec >= 0;
    if (!usable) {
        if (f_.sched != Sched::Doors || f_.planSeqno != -2) {
            logInfo("purser: no valid SimBrief plan: boarding starts when a door has been open %.0f s",
                    static_cast<double>(set.boardingDoorSettle));
            f_.planSeqno = -2;
            f_.planAt = -1;
        }
        f_.sched = Sched::Doors;
        doorsBoarding(s, set, atGate);
        return;
    }
    if (plan_.seqno != f_.planSeqno || plan_.boardingAtSec != f_.planAt) {
        // A plan this flight hasn't evaluated: before its boarding time we wait; after it (late load) the doors rule.
        // A re-timed plan while already waiting at the gate keeps waiting, so a time that moved into the past starts
        // boarding now rather than falling back to the doors.
        f_.planSeqno = plan_.seqno;
        f_.planAt = plan_.boardingAtSec;
        const double until = secondsUntil(plan_.boardingAtSec, s.zuluSec);
        f_.sched = (until > 0 || f_.sched == Sched::Waiting) ? Sched::Waiting : Sched::Doors;
        if (f_.sched == Sched::Waiting) {
            note(s.t, "boarding at " + zuluLabel(plan_.boardingAtSec) + " (SimBrief, in " + mmss(until) + ")");
        } else {
            char buf[160];
            std::snprintf(buf, sizeof(buf), "the SimBrief boarding time %s has passed: boarding starts when a door has been open %.0f s",
                          zuluLabel(plan_.boardingAtSec).c_str(), static_cast<double>(set.boardingDoorSettle));
            note(s.t, buf);
        }
    }
    if (f_.sched == Sched::Waiting) {
        if (secondsUntil(plan_.boardingAtSec, s.zuluSec) > 0) return;
        if (atGate) {
            f_.sched = Sched::Unknown;
            startBoarding(s, "SimBrief boarding time");
            return;
        }
        note(s.t, "SimBrief boarding time reached away from the gate: doors rule");
        f_.sched = Sched::Doors;
    }
    doorsBoarding(s, set, atGate);
}

void Purser::doorsBoarding(const SimState& s, const Settings& set, bool atGate)
{
    // D-1: a passenger door open at the gate with AC power for boarding_door_settle_s.
    if (!atGate || !s.acPower) {
        f_.doorOpenSince = -1;
        return;
    }
    if (s.doorMax > 0.5f) {
        if (f_.doorOpenSince < 0) f_.doorOpenSince = s.t;
    } else {
        f_.doorOpenSince = -1;
    }
    const bool gapOk = f_.turnaroundAt < 0 || s.t >= f_.turnaroundAt;
    if (f_.doorOpenSince >= 0 && s.t - f_.doorOpenSince >= set.boardingDoorSettle && gapOk) startBoarding(s, "doors open");
}

void Purser::startBoarding(const SimState& s, const char* why)
{
    f_.boarding = true;
    f_.doorsSeenOpen = s.doorMax > 0.5f;
    f_.nextWelcome = s.t + randIn(20, 45);
    f_.turnaroundAt = -1;
    note(s.t, std::string("boarding started (") + why + ")");
}

void Purser::onCabinCall(const SimState& s, const Settings& set)
{
    // VP_SPEC § 4.3a. Each branch logs what the call did.
    const bool atGate = s.onGround && s.enginesRunning == 0 && s.gsKt < 1;
    if (!f_.takeoff && !f_.boarding && !f_.boardingComplete && !f_.armDoors && atGate) {
        if (!s.acPower) {
            logInfo("purser: cabin call ignored: no AC power in the cabin");
            return;
        }
        startBoarding(s, "cabin call");
        return;
    }
    const double quick = s.t + set.expediteSeconds;
    if (!f_.takeoff && f_.armDoors && !f_.takeoffCallsQueued) {
        if (f_.takeoffCheckDone >= 0) {
            f_.takeoffCheckDone = std::min(f_.takeoffCheckDone, quick);
            logInfo("purser: cabin call: cabin check before takeoff expedited");
        } else {
            f_.expediteTakeoffCheck = true;
            logInfo("purser: cabin call: the cabin check after the safety briefing will be quick");
        }
        return;
    }
    if (f_.flown && f_.descentEntered && !f_.landingCallsQueued) {
        if (f_.landingCheckDone >= 0) {
            f_.landingCheckDone = std::min(f_.landingCheckDone, quick);
            logInfo("purser: cabin call: cabin check before landing expedited");
        } else {
            f_.expediteLandingCheck = true;
            logInfo("purser: cabin call: the cabin check before landing will be quick");
        }
        return;
    }
    logInfo("purser: cabin call: nothing to start or expedite now");
}

// ---- in flight -------------------------------------------------------------------

void Purser::flight(const SimState& s, const Settings& set)
{
    if (!f_.takeoff) return;
    const bool airborne = !s.onGround;

    if (!f_.climbEntered && airborne &&
        (phase_ == Phase::Climb || phase_ == Phase::Cruise || (estimating_ && s.aglFt > 1500))) {
        f_.climbEntered = true;
        f_.afterTakeoffDue = s.t + randIn(set.afterTakeoffMin, set.afterTakeoffMax);
    }
    if (f_.afterTakeoffDue >= 0 && !f_.afterTakeoff && s.t >= f_.afterTakeoffDue) {
        f_.afterTakeoff = true;
        enqueue(AnnType::AfterTakeoff, CallContext::Any, ExpireOnGround);
    }

    // Seatbelt sign transitions in the air, debounced
    if (airborne && f_.flown) {
        if (s.seatbelt != f_.beltState) {
            if (!f_.beltPending) {
                f_.beltPending = true;
                f_.beltChangedAt = s.t;
            }
            if (s.t - f_.beltChangedAt >= set.seatbeltDebounce) {
                f_.beltState = s.seatbelt;
                f_.beltPending = false;
                // Not before AfterTakeoff: hold until just after it is due.
                const double after = f_.afterTakeoff ? 0 : (f_.afterTakeoffDue >= 0 ? f_.afterTakeoffDue + 0.1 : s.t + 60);
                if (s.seatbelt) {
                    const bool nearDescent = atLeast(phase_, Phase::Descent) ||
                                             (f_.descentAt >= 0 && std::fabs(s.t - f_.descentAt) < kSeatbeltDescentWindow);
                    if (!nearDescent) enqueue(AnnType::SeatbeltSignOn, CallContext::Any, ExpireOnGround, after);
                    else logDebug("purser: seatbelt sign on near the descent: covered by DescentStarted");
                } else {
                    enqueue(AnnType::SeatbeltSignOff, CallContext::Any, ExpireOnGround, after);
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
    if (f_.cabinServiceDue >= 0 && !f_.cabinService && s.t >= f_.cabinServiceDue) {
        f_.cabinService = true;
        if (phase_ == Phase::Cruise && !f_.decompression) enqueue(AnnType::CabinService, CallContext::Any, ExpireOnGround);
        else log(s.t, AnnType::CabinService, CallContext::Any, Outcome::Skipped, "no longer in cruise");
    }

    if (!f_.descentEntered && airborne && (phase_ == Phase::Descent || phase_ == Phase::Approach)) {
        f_.descentEntered = true;
        f_.descentAt = s.t;
        f_.descentDue = s.t + randIn(set.descentMin, set.descentMax);
        if (!f_.cabinService && f_.cabinServiceDue >= 0) {
            f_.cabinService = true;
            log(s.t, AnnType::CabinService, CallContext::Any, Outcome::Skipped, "descent started first");
        }
    }
    if (f_.descentDue >= 0 && !f_.descentStarted && s.t >= f_.descentDue) {
        f_.descentStarted = true;
        enqueue(AnnType::DescentStarted, CallContext::Any, ExpireOnGround);
    }

    if (f_.descentFinished && f_.landingCheckDone < 0) {
        const double d = checkDuration(s.pax, set);
        f_.landingCheckDone = s.t + (f_.expediteLandingCheck ? std::min<double>(d, set.expediteSeconds) : d);
    }
    const bool window = airborne && (phase_ == Phase::Approach || s.aglFt < set.landingReadyAglFt || f_.forceLandingCalls) &&
                        s.aglFt > set.landingDropAglFt;
    if (f_.landingCheckDone >= 0 && !f_.landingCallsQueued && s.t >= f_.landingCheckDone && window) {
        f_.landingCallsQueued = true;
        f_.forceLandingCalls = false;
        enqueue(AnnType::CrewTakeSeats, CallContext::Landing, ExpireLandingWindow);
        if (dark(s, set)) enqueue(AnnType::CabinDim, CallContext::Landing, ExpireLandingWindow);
        enqueue(AnnType::CabinSecured, CallContext::Landing, ExpireLandingWindow);
    }

    if (phase_ == Phase::GA && prevPhase_ != Phase::GA) {
        logInfo("purser: go-around: pending landing calls dropped");
        dropWhere(ExpireLandingWindow, Outcome::Expired);
    }
}

// ---- arrival ---------------------------------------------------------------------

void Purser::arrival(const SimState& s, const Settings& set)
{
    if (!f_.flown) return;
    // Touchdown: on the ground after the flight, or the ECAM touchdown phase (VP_SPEC § 4.1).
    const bool ecamDown = s.ecamPhase == 8 || s.ecamPhase == 9;
    const bool down = s.onGround || (ecamDown && s.aglFt < 50);
    if (!down && s.aglFt > 50 && f_.touchdownAt >= 0 && !f_.afterLanding) f_.touchdownAt = -1;   // touch-and-go
    if (down && f_.touchdownAt < 0 && !f_.afterLanding) {
        f_.touchdownAt = s.t;
        note(s.t, std::string("touchdown (") + (s.onGround ? "on ground" : "ECAM flight phase") + ")");
        dropWhere(ExpireOnGround | ExpireLandingWindow, Outcome::Expired);
    }
    if (f_.touchdownAt < 0 || !down) return;

    if (!f_.afterLanding) {
        const bool stowed = s.speedbrake <= 0.01f && s.groundSpoilers != 1;
        const bool normal = stowed && !s.landingLights && s.gsKt < 40;
        const bool fallback = s.t - f_.touchdownAt >= set.afterLandingFallback && s.gsKt < 30;
        if (normal || fallback) {
            f_.afterLanding = true;
            enqueue(AnnType::AfterLanding);
        }
    }
    if (!f_.disarm) {
        const bool slidesFalling = s.slidesKnown && !s.slidesArmed && f_.slidesWere;
        const bool atStand = s.gsKt < 1 && (s.parkBrake || s.chocks) && (!s.beacon || !s.seatbelt);
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
        (s.doorFwd > 0.5f || s.doorMax > 0.5f || s.t - f_.disarmAt >= set.deboardFallback)) {
        f_.deboarding = true;
        f_.deboardAt = s.t;
        f_.deboardDone = s.t + std::max<double>(set.deboardMin, s.pax * set.deboardPerPax * type_.typeFactor);
        enqueue(AnnType::DeboardingStarted);
    }
    if (f_.deboarding && !f_.deboardComplete && s.t >= f_.deboardDone) {
        f_.deboardComplete = true;
        enqueue(AnnType::DeboardingComplete);
    }
    if (f_.deboardComplete && queue_.empty() && !current_) {
        // Turnaround: a new flight. Boarding restarts after the gap, or when the doors close and reopen.
        logInfo("purser: turnaround, next boarding in %.0f s or when the doors reopen", static_cast<double>(set.turnaroundGap));
        Flags fresh;
        fresh.inferred = true;
        fresh.turnaroundAt = s.t + set.turnaroundGap;
        fresh.beltState = s.seatbelt;
        fresh.slidesWere = s.slidesArmed;
        f_ = fresh;
    }
}

// ---- queue and interphone ---------------------------------------------------------

void Purser::runCall(const SimState& s, const Settings& set)
{
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
                r->detail = *label + (call_.forceAnswer ? " (advanced)" : " (CAB answered)");
                if (sink_) sink_(*r);
                break;
            }
        return;
    }
    if (s.t >= call_.nextChime) {
        if (++call_.attempt > set.interphoneRetries) {
            finishCurrent(Outcome::Unanswered, "no answer after " + std::to_string(set.interphoneRetries) + " retries");
            return;
        }
        out_.chime();
        call_.nextChime = s.t + set.interphoneRetry;
    }
}

void Purser::runQueue(const SimState& s, const Settings& set)
{
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
    if (!s.acPower || s.replay || s.t < gapUntil_) return;   // D-13: no PA without power

    while (true) {
        auto ready = std::find_if(queue_.begin(), queue_.end(), [&](const Item& i) { return i.notBefore <= s.t; });
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
            // VP_SPEC § 4.3: chime, then wait for the CAB knob.
            current_ = it;
            call_ = Call{};
            call_.active = true;
            call_.item = it;
            call_.attempt = 0;
            call_.nextChime = s.t + set.interphoneRetry;
            out_.chime();
            log(s.t, it.type, it.ctx, Outcome::Queued, "cabin calling: pull CAB on the ACP");
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

// ---- tick, advance, labels -----------------------------------------------------------

void Purser::updateMusic(const SimState& s)
{
    const bool want = s.acPower && !s.replay &&
                      ((f_.boarding && !f_.boardingComplete) || (f_.deboarding && !f_.deboardComplete));
    if (want != music_) {
        music_ = want;
        out_.setMusic(want);
    }
}

void Purser::applyAdvance(const SimState& s, const Settings& set)
{
    const double t = s.t;
    gapUntil_ = t;
    if (call_.active && !call_.talking) {
        call_.forceAnswer = true;
        return;
    }
    // The soonest pending timer or delayed item becomes due now.
    double* best = nullptr;
    double bestAt = 1e300;
    auto consider = [&](double& at, bool live) {
        if (live && at > t && at < bestAt) {
            bestAt = at;
            best = &at;
        }
    };
    double preSafetyAt = f_.armDoorsAt + set.preSafetyDelay;
    consider(preSafetyAt, f_.armDoorsFinished && !f_.safetyQueued);
    consider(f_.takeoffCheckDone, !f_.takeoffCallsQueued && f_.takeoffCheckDone >= 0);
    consider(f_.afterTakeoffDue, !f_.afterTakeoff && f_.afterTakeoffDue >= 0);
    consider(f_.cabinServiceDue, !f_.cabinService && f_.cabinServiceDue >= 0);
    consider(f_.descentDue, !f_.descentStarted && f_.descentDue >= 0);
    consider(f_.landingCheckDone, !f_.landingCallsQueued && f_.landingCheckDone >= 0);
    consider(f_.deboardDone, f_.deboarding && !f_.deboardComplete);
    consider(f_.turnaroundAt, f_.turnaroundAt >= 0);
    for (Item& i : queue_) consider(i.notBefore, true);
    if (best) {
        if (best == &preSafetyAt) f_.armDoorsAt = t - set.preSafetyDelay;
        else *best = t;
        return;
    }
    if (!f_.boarding && !f_.boardingComplete && !f_.armDoors && !f_.takeoff && s.onGround && s.acPower) {
        startBoarding(s, "advanced");
        return;
    }
    if (f_.boarding && !f_.boardingComplete) {
        f_.forceBoardingComplete = true;
        return;
    }
    if (f_.landingCheckDone >= 0 && !f_.landingCallsQueued) {
        f_.forceLandingCalls = true;
        return;
    }
    if (f_.disarm && !f_.deboarding) {
        f_.disarmAt = t - set.deboardFallback;
        return;
    }
    logInfo("purser: nothing to advance");
}

void Purser::tick(const SimState& s, const Settings& set)
{
    now_ = s.t;
    zulu_ = s.zuluSec;
    preSafetyDelay_ = set.preSafetyDelay;
    doorSettle_ = set.boardingDoorSettle;
    mode_ = set.boarding();
    if (!f_.inferred) infer(s);

    // Lift-off: 5 s above 50 ft AGL, or the ECAM lift-off/climb phases after its takeoff phases (VP_SPEC § 4.2).
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
    if (!f_.flown && f_.ecamTakeoffSeen && s.ecamPhase >= 5 && s.ecamPhase <= 7 && !s.onGround) {
        f_.flown = true;
        f_.liftoffAt = s.t;
        note(s.t, "lift-off (ECAM flight phase " + std::to_string(s.ecamPhase) + ")");
    }

    // Phase: the FMGS, or an estimate when it is missing or was never initialized (D-20).
    const bool wantEstimate = estimateWanted(s, set);
    if (wantEstimate && !estimating_ && !f_.estimateLogged && s.fmgsPhase) {
        f_.estimateLogged = true;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "the FMGS phase is still PreFlight %.0f s after lift-off (FMGS not initialized): estimating the phase",
                      static_cast<double>(set.noFmgsAfterS));
        note(s.t, buf);
    }
    estimating_ = wantEstimate;
    phase_ = estimating_ ? estimatePhase(s) : *s.fmgsPhase;

    if (!f_.decompression && s.paxMasks && s.mslFt > 10000) {
        // Decompression pre-empts everything (VP_SPEC § 4.1).
        f_.decompression = true;
        if (current_) {
            out_.stop();
            finishCurrent(Outcome::Stopped, "decompression");
        }
        for (const Item& i : queue_) log(s.t, i.type, i.ctx, Outcome::Expired, "decompression");
        queue_.clear();
        f_.cabinService = true;
        queue_.push_front(Item{ AnnType::Decompression, CallContext::Any, ExpireOnGround, 0 });
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

std::string Purser::stageLabel() const
{
    if (f_.decompression && f_.touchdownAt < 0) return "Decompression";
    if (f_.deboarding) return f_.deboardComplete ? "Deboarding complete" : "Deboarding";
    if (f_.disarm) return "At the stand";
    if (f_.touchdownAt >= 0) return "Landed, taxiing in";
    if (f_.flown || f_.takeoff) {
        switch (phase_) {
        case Phase::Takeoff: return "Takeoff";
        case Phase::Climb: return "Climb";
        case Phase::Cruise: return "Cruise";
        case Phase::Descent: return "Descent";
        case Phase::Approach: return f_.landingCallsQueued ? "Approach, cabin secured" : "Approach";
        case Phase::GA: return "Go-around";
        default: return "Airborne";
        }
    }
    if (f_.takeoffCallsQueued) return "Cabin secured for takeoff";
    if (f_.safetyFinished) return "Cabin check before takeoff";
    if (f_.safetyQueued) return "Safety briefing";
    if (f_.armDoors) return "Doors armed";
    if (f_.boardingComplete) return "Boarding complete";
    if (f_.boarding) return "Boarding";
    if (f_.turnaroundAt >= 0) return "Turnaround";
    return "At the gate";
}

std::vector<std::string> Purser::pendingLabels() const
{
    std::vector<std::pair<double, std::string>> v;
    const double t = now_;
    auto add = [&](double at, const std::string& what) { v.emplace_back(at, what + (at > t ? " in " + mmss(at - t) : " now")); };
    for (const Item& i : queue_)
        add(std::max(i.notBefore, t), std::string(annTypeName(i.type)) + (i.ctx != CallContext::Any ? std::string(" (") + contextName(i.ctx) + ")" : ""));
    if (f_.boarding && !f_.boardingComplete) {
        add(f_.nextWelcome, "BoardingWelcome");
        v.emplace_back(t + 1e6, "BoardingComplete when the doors close or the beacon comes on");
    }
    if (!f_.armDoors && f_.boardingComplete) v.emplace_back(t + 1e6, "ArmDoors on beacon, pushback or engine start");
    if (!f_.boarding && !f_.boardingComplete && !f_.armDoors && !f_.takeoff) {
        if (mode_ == BoardingStart::Schedule && f_.sched == Sched::Waiting && zulu_ >= 0) {
            const double until = secondsUntil(plan_.boardingAtSec, zulu_);
            add(t + std::max(0.0, until), "Boarding at " + zuluLabel(plan_.boardingAtSec) + " (SimBrief)");
        } else if (mode_ == BoardingStart::Call) {
            v.emplace_back(t + 2e6, "Boarding starts on a cabin call (overhead CALLS: FWD, AFT, ALL...)");
        } else {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "Boarding starts when a door has been open %.0f s, or on a cabin call",
                          static_cast<double>(doorSettle_));
            v.emplace_back(t + 2e6, buf);
        }
    }
    if (f_.armDoorsFinished && !f_.safetyQueued) add(f_.armDoorsAt + preSafetyDelay_, "PreSafetyBriefing");
    if (f_.takeoffCheckDone >= 0 && !f_.takeoffCallsQueued) add(f_.takeoffCheckDone, "CrewTakeSeats (takeoff), cabin check ends");
    if (f_.afterTakeoffDue >= 0 && !f_.afterTakeoff) add(f_.afterTakeoffDue, "AfterTakeoff");
    if (f_.cabinServiceDue >= 0 && !f_.cabinService) add(f_.cabinServiceDue, "CabinService");
    if (f_.descentDue >= 0 && !f_.descentStarted) add(f_.descentDue, "DescentStarted");
    if (f_.landingCheckDone >= 0 && !f_.landingCallsQueued) add(f_.landingCheckDone, "CrewTakeSeats (landing), approach or below the ready height");
    if (f_.deboarding && !f_.deboardComplete) add(f_.deboardDone, "DeboardingComplete");
    if (f_.turnaroundAt >= 0 && !f_.boarding) add(f_.turnaroundAt, "Next boarding");
    std::stable_sort(v.begin(), v.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::string> out;
    for (auto& p : v) out.push_back(std::move(p.second));
    return out;
}

std::string Purser::nextLabel() const
{
    const auto p = pendingLabels();
    return p.empty() ? std::string("-") : p.front();
}

std::string Purser::nowLabel() const
{
    if (!current_) return "-";
    std::string s = annTypeName(current_->type);
    if (current_->ctx != CallContext::Any) s += std::string(" (") + contextName(current_->ctx) + ")";
    if (call_.active && !call_.talking) s += ": cabin calling, attempt " + std::to_string(call_.attempt + 1);
    return s;
}

} // namespace fxa
