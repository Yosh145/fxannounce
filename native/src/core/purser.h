// The Virtual Purser: turns SimState into announcements (VP_SPEC.md § 4). Pure C++; audio goes through PurserOutput
// so whole flights can be replayed in tests with a fake player.
#pragma once

#include "core/aircraft.h"
#include "core/announcement.h"
#include "core/config.h"
#include "core/sim_state.h"
#include "core/simbrief.h"

#include <deque>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace fxa {

class PurserOutput {
public:
    virtual ~PurserOutput() = default;
    // Starts an announcement. Returns a label of the file that plays, or nullopt if the active pack has no eligible
    // file (the announcement is skipped, Fenix rule).
    virtual std::optional<std::string> play(AnnType type, CallContext ctx, bool interphone) = 0;
    // True while the announcement started last is still playing.
    virtual bool playing() const = 0;
    virtual void stop() = 0;
    // The cabin call chime (interphone).
    virtual void chime() = 0;
    // Boarding music wanted (the player pauses it under announcements by itself).
    virtual void setMusic(bool on) = 0;
    // The mode of the file that would play (Interphone files go through the cabin call), or nullopt if the pack has
    // no eligible file.
    virtual std::optional<Mode> peek(AnnType type, CallContext ctx) const = 0;
};

enum class Outcome { Queued, Playing, Played, Skipped, NoSound, Expired, Disabled, Stopped, Unanswered };
const char* outcomeName(Outcome o);

struct TimelineRow {
    double t = 0;
    AnnType type = AnnType::BoardingWelcome;
    CallContext context = CallContext::Any;
    Outcome outcome = Outcome::Queued;
    std::string detail;   // file label or reason
};

class Purser {
public:
    explicit Purser(PurserOutput& out, unsigned seed = std::random_device{}());

    void setAircraft(const AircraftType& t) { type_ = t; }
    // The SimBrief boarding schedule (VP_SPEC § 4.1a); an invalid plan means the doors rule.
    void setBoardingPlan(const BoardingPlan& p) { plan_ = p; }
    // Called every tick with the newest snapshot. `s` must not be paused-time-advanced (SimState::t freezes).
    void tick(const SimState& s, const Settings& set);

    // Fresh flight; the next tick infers how far the flight already is (mid-session start, teleport).
    void reset();
    // UI actions: stop the current announcement (the sequence continues) / fire the next pending event now.
    void skip();
    void advance();
    // A cockpit cabin call (overhead CALLS panel, not MECH): starts boarding at the gate, or expedites the cabin
    // safety check (VP_SPEC § 4.3a). Takes effect on the next tick.
    void cabinCall() { callPressed_ = true; }

    // The pack may change only before ArmDoors (VP_SPEC § 1.4).
    bool packLocked() const { return f_.armDoors; }
    Phase phase() const { return phase_; }
    // True while the phase is estimated instead of read from the FMGS (D-20).
    bool phaseEstimated() const { return estimating_; }
    std::string stageLabel() const;
    std::string nextLabel() const;
    std::string nowLabel() const;
    const std::deque<TimelineRow>& timeline() const { return timeline_; }
    bool calling() const { return call_.active && !call_.talking; }
    // The next queued announcement (for decode prefetch).
    std::optional<std::pair<AnnType, CallContext>> nextQueued() const
    {
        if (queue_.empty()) return std::nullopt;
        return std::make_pair(queue_.front().type, queue_.front().ctx);
    }
    // Pending items and timers, soonest first, for the UI ("PreSafetyBriefing in 0:42").
    std::vector<std::string> pendingLabels() const;
    // Called for every timeline row as it is written or updated (log and recorder).
    void setEventSink(std::function<void(const TimelineRow&)> sink) { sink_ = std::move(sink); }
    // Called with the purser's decisions that are not announcements (boarding start, takeoff thrust and which signal,
    // touchdown, the phase estimate), for the recorder.
    void setNoteSink(std::function<void(double, const std::string&)> sink) { noteSink_ = std::move(sink); }

private:
    struct Item {
        AnnType type;
        CallContext ctx = CallContext::Any;
        int expire = 0;           // bitmask of Expire* below
        double notBefore = 0;
    };
    enum : int { ExpireAtTakeoff = 1, ExpireLandingWindow = 2, ExpireAirborne = 4, ExpireOnGround = 8 };
    // Where the schedule mode stands for this flight's boarding (VP_SPEC § 4.1a).
    enum class Sched { Unknown, Waiting, Doors };

    struct Flags {
        bool inferred = false;
        // ground, departure
        double doorOpenSince = -1, doorsClosedSince = -1, armWaitSince = -1;
        bool boarding = false, boardingComplete = false, armDoors = false, armDoorsFinished = false;
        double armDoorsAt = -1, nextWelcome = -1;
        bool safetyQueued = false, safetyFinished = false;
        double takeoffCheckDone = -1;
        bool takeoffCallsQueued = false;
        bool takeoff = false, flown = false;
        double airborneSince = -1;
        // flight
        double afterTakeoffDue = -1;
        bool afterTakeoff = false, climbEntered = false;
        double cabinServiceDue = -1;
        bool cruiseEntered = false, cabinService = false;
        double descentDue = -1, descentAt = -1;
        bool descentEntered = false, descentStarted = false, descentFinished = false;
        double landingCheckDone = -1;
        bool landingCallsQueued = false;
        bool decompression = false;
        // arrival
        double touchdownAt = -1;
        bool afterLanding = false, disarm = false;
        double disarmAt = -1, deboardAt = -1, deboardDone = -1;
        bool deboarding = false, deboardComplete = false;
        double turnaroundAt = -1;
        // seatbelt debounce
        bool beltState = false;
        double beltChangedAt = -1;
        bool beltPending = false;
        // slides edge detection
        bool slidesWere = false;
        // advance() overrides
        bool forceBoardingComplete = false, forceLandingCalls = false;
        // boarding and cabin calls
        bool doorsSeenOpen = false;
        bool expediteTakeoffCheck = false, expediteLandingCheck = false;
        // SimBrief schedule: the plan evaluated (seqno, boarding time) and the outcome
        Sched sched = Sched::Unknown;
        int planSeqno = -1;
        double planAt = -1;
        // cross-checks and the no-FMGS estimate
        bool ecamTakeoffSeen = false;
        double liftoffAt = -1;
        bool estimateLogged = false;
        double levelSince = -1;
    };

    struct Call {
        bool active = false;
        Item item{};
        int attempt = 0;
        double nextChime = 0;
        bool talking = false;
        bool forceAnswer = false;
    };

    Phase estimatePhase(const SimState& s);
    bool estimateWanted(const SimState& s, const Settings& set) const;
    void scheduleBoarding(const SimState& s, const Settings& set, bool atGate);
    void doorsBoarding(const SimState& s, const Settings& set, bool atGate);
    void infer(const SimState& s);
    void departure(const SimState& s, const Settings& set);
    void flight(const SimState& s, const Settings& set);
    void arrival(const SimState& s, const Settings& set);
    void runQueue(const SimState& s, const Settings& set);
    void runCall(const SimState& s, const Settings& set);
    void enqueue(AnnType t, CallContext c = CallContext::Any, int expire = 0, double notBefore = 0);
    void dropWhere(int expireMask, Outcome why);
    bool expired(const Item& it, const SimState& s, const Settings& set) const;
    void log(double t, AnnType type, CallContext c, Outcome o, std::string detail = {});
    void finishCurrent(Outcome o, std::string detail = {});
    void onFinished(const Item& it);
    void applyAdvance(const SimState& s, const Settings& set);
    void updateMusic(const SimState& s);
    void startBoarding(const SimState& s, const char* why);
    void onCabinCall(const SimState& s, const Settings& set);
    double checkDuration(int pax, const Settings& set);
    double randIn(double lo, double hi);
    bool dark(const SimState& s, const Settings& set) const { return s.sunPitchDeg < set.darkSunDeg; }

    PurserOutput& out_;
    std::mt19937 rng_;
    AircraftType type_ = makeAircraftType(Family::A320, false);
    Flags f_;
    Phase phase_ = Phase::PreFlight;
    Phase prevPhase_ = Phase::PreFlight;
    std::deque<Item> queue_;
    std::optional<Item> current_;
    double gapUntil_ = 0;
    double now_ = 0;
    Call call_;
    bool skipRequested_ = false, advanceRequested_ = false, callPressed_ = false;
    BoardingStart mode_ = BoardingStart::Schedule;
    BoardingPlan plan_;
    bool estimating_ = false;
    double zulu_ = -1;
    float doorSettle_ = 15;
    std::deque<TimelineRow> timeline_;
    std::function<void(const TimelineRow&)> sink_;
    std::function<void(double, const std::string&)> noteSink_;
    void note(double t, const std::string& text);
    bool music_ = false;
    float preSafetyDelay_ = 60;
};

} // namespace fxa
