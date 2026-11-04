// virtual purser vp_spec 4 simstate in announcements out thru purseroutput so
// tests can replay whole flights w a fake player
#pragma once

#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "core/aircraft.h"
#include "core/announcement.h"
#include "core/config.h"
#include "core/sim_state.h"
#include "core/simbrief.h"

namespace fxa {

class PurserOutput {
 public:
  virtual ~PurserOutput() = default;
  // label of the file that plays or nullopt when the pack has nothing eligible
  // fenix just skips it
  virtual std::optional<std::string> play(AnnType type, CallContext ctx,
                                          bool interphone) = 0;
  virtual bool playing() const = 0;
  virtual void stop() = 0;
  // cockpit call buzzer vp_spec 4.3
  virtual void chime() = 0;
  // player pauses music under announcements by itself
  virtual void setMusic(bool on) = 0;
  // nullopt nothing eligible interphone files go thru the cabin call
  virtual std::optional<Mode> peek(AnnType type, CallContext ctx) const = 0;
};

enum class Outcome {
  Queued,
  Playing,
  Played,
  Skipped,
  NoSound,
  Expired,
  Disabled,
  Stopped,
  Unanswered
};
const char* outcomeName(Outcome o);

struct TimelineRow {
  double t = 0;
  AnnType type = AnnType::BoardingWelcome;
  CallContext context = CallContext::Any;
  Outcome outcome = Outcome::Queued;
  std::string detail;   // file or reason
  double zuluSec = -1;  // secs since midnight -1 unknown
  double localSec = -1;
};

// timeline coming up entry announcement or milestone like boarding at 12:39z
struct PendingItem {
  std::optional<AnnType> type;
  CallContext context = CallContext::Any;
  std::string what;
  std::string note;
  double inSec = -1;  // -1 waits for condition
  std::string condition;
};

class Purser {
 public:
  explicit Purser(PurserOutput& out, unsigned seed = std::random_device{}());

  void setAircraft(const AircraftType& t) { type_ = t; }
  // vp_spec 4.1a invalid plan doors rule
  void setBoardingPlan(const BoardingPlan& p) { plan_ = p; }
  // every tick s.t freezes while paused
  void tick(const SimState& s, const Settings& set);

  // next tick works out how far along the flight already is mid session start
  // teleport etc
  void reset();
  void skip();
  void advance();
  // overhead calls panel not mech starts/ends boarding hurries the check lineup
  // signal once taxied or crew may leave seats after takeoff vp_spec 4.3a
  // applied next tick
  void cabinCall() { callPressed_ = true; }

  // vp_spec 1.4
  bool packLocked() const { return f_.armDoors; }
  // ofp latched from here on 4.2a
  bool tookOff() const { return f_.takeoff || f_.flown; }
  // deboarding done w turnaround off 4.4
  bool flightComplete() const { return f_.flightComplete; }
  Phase phase() const { return phase_; }
  // d20
  bool phaseEstimated() const { return estimating_; }
  std::string stageLabel() const;
  std::string nextLabel() const;
  std::string nowLabel() const;
  std::optional<std::pair<AnnType, CallContext>> current() const {
    if (!current_) return std::nullopt;
    return std::make_pair(current_->type, current_->ctx);
  }
  // 1 based
  int callAttempt() const { return call_.attempt + 1; }
  const std::deque<TimelineRow>& timeline() const { return timeline_; }
  bool calling() const { return call_.active && !call_.talking; }
  // for decode prefetch
  std::optional<std::pair<AnnType, CallContext>> nextQueued() const {
    if (queue_.empty()) return std::nullopt;
    return std::make_pair(queue_.front().type, queue_.front().ctx);
  }
  // timed ones first then the ones waiting on a condition
  std::vector<PendingItem> pending() const;
  // fenix names log tests
  std::vector<std::string> pendingLabels() const;
  // shortened boarding or xp clock not matching the ofp
  const std::string& boardingNote() const { return boardingNote_; }
  // -1 when not boarding vp_spec 4.1b
  double boardingEndsAt() const {
    return f_.boarding && !f_.boardingComplete ? f_.boardingEndAt : -1;
  }
  double boardingStartedAt() const {
    return f_.boarding && !f_.boardingComplete ? f_.boardingStartAt : -1;
  }
  // log recorder
  void setEventSink(std::function<void(const TimelineRow&)> sink) {
    sink_ = std::move(sink);
  }
  // non announcement decisions boarding start which takeoff signal touchdown
  // phase estimate for the recorder
  void setNoteSink(std::function<void(double, const std::string&)> sink) {
    noteSink_ = std::move(sink);
  }

 private:
  struct Item {
    AnnType type;
    CallContext ctx = CallContext::Any;
    int expire = 0;  // expire bits
    double notBefore = 0;
  };
  enum : int {
    ExpireAtTakeoff = 1,
    ExpireLandingWindow = 2,
    ExpireAirborne = 4,
    ExpireOnGround = 8
  };
  // 4.1a
  enum class Sched { Unknown, Waiting, Doors };

  struct Flags {
    bool inferred = false;
    double settleFrom = -1;  // vp_spec 4.4
    bool hotStart =
        false;  // started parked w engines on no boarding till theyre shut down
    // ground departure
    double armWaitSince = -1;
    bool boarding = false, boardingComplete = false, armDoors = false,
         armDoorsFinished = false;
    // modeled boarding 4.1b
    double boardingNatural = 0, boardingStartAt = -1, boardingEndAt = -1,
           boardingStartZulu = -1;
    int boardingPlanSeqno = -1;
    // doors rule
    bool doorSeenOpenAtGate = false;
    double readySince = -1;
    double armDoorsAt = -1, nextWelcome = -1;
    bool safetyQueued = false, safetyFinished = false;
    // d22 first movement after boarding random delay to the briefing
    double movedAt = -1, safetyAfterMove = 30;
    bool taxied = false;
    double takeoffCheckDone = -1;
    // d23
    bool takeoffSecuredQueued = false, lineup = false;
    bool takeoffCallsQueued = false;
    bool takeoff = false, flown = false;
    double airborneSince = -1;
    // flight
    double afterTakeoffDue = -1;
    bool afterTakeoff = false, climbEntered = false;
    double cabinServiceDue = -1;
    bool cruiseEntered = false, cabinService = false;
    double descentDue = -1, descentAt = -1;
    bool descentEntered = false, descentStarted = false,
         descentFinished = false;
    double landingCheckDone = -1;
    // d24 cabinsecured in the approach window seats dim on ns flick or gear
    // down
    bool landingSecuredQueued = false, seatsLanding = false;
    bool landingCallsQueued = false;
    bool decompression = false;
    // arrival
    double touchdownAt = -1;
    bool afterLanding = false, disarm = false;
    double disarmAt = -1, deboardAt = -1, deboardDone = -1;
    bool deboarding = false, deboardComplete = false;
    double turnaroundAt = -1;
    bool gapSawClosed = false;  // a reopen ends the gap
    // turnaround off d26 new ofp starts next flight
    bool flightComplete = false;
    int flightSeqno = -1;
    // seatbelt debounce
    bool beltState = false;
    double beltChangedAt = -1;
    bool beltPending = false;
    bool slidesWere = false;
    bool forceBoardingComplete = false, forceLandingCalls = false;
    bool expediteTakeoffCheck = false, expediteLandingCheck = false;
    // schedule mode plan seen outcome
    Sched sched = Sched::Unknown;
    int planSeqno = -1;
    double planAt = -1;
    bool clockMismatch = false;  // more than max_wait_min ahead to doors rule
    // ns flick 4.2
    int nsLast = -1;
    double nsOffAt = -1;
    bool fmgsLeftPreflight = false;
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
  void enqueue(AnnType t, CallContext c = CallContext::Any, int expire = 0,
               double notBefore = 0);
  void dropWhere(int expireMask, Outcome why);
  bool expired(const Item& it, const SimState& s, const Settings& set) const;
  void log(double t, AnnType type, CallContext c, Outcome o,
           std::string detail = {});
  void finishCurrent(Outcome o, std::string detail = {});
  void onFinished(const Item& it);
  void applyAdvance(const SimState& s, const Settings& set);
  void updateMusic(const SimState& s);
  void startBoarding(const SimState& s, const Settings& set, const char* why);
  double boardingEnd(const SimState& s, const Settings& set);
  // xp clock jumped user set the time
  void watchClock(const SimState& s);
  void onCabinCall(const SimState& s, const Settings& set);
  // lit unlit lit inside ns_flick_max_s
  bool detectFlick(const SimState& s, const Settings& set);
  // d23 d24 d25
  void signalLineup(const SimState& s, const Settings& set,
                    const std::string& why);
  void signalSeatsLanding(const SimState& s, const Settings& set,
                          const std::string& why);
  void signalAfterTakeoff(const SimState& s, const std::string& why);
  // d22 never too soon after armdoors
  double safetyDue() const {
    return std::max(f_.movedAt + f_.safetyAfterMove,
                    f_.armDoorsAt + preSafetyDelay_);
  }
  // next flight at the gate turnaround off new ofp or engines shut down after a
  // hot start
  void freshFlight(const SimState& s);
  double checkDuration(int pax, const Settings& set);
  double randIn(double lo, double hi);
  bool dark(const SimState& s, const Settings& set) const {
    return s.sunPitchDeg < set.darkSunDeg;
  }

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
  double local_ = -1;
  double lastZulu_ = -1, lastZuluT_ = -1;
  std::string boardingNote_;
  float doorSettle_ = 15;
  std::deque<TimelineRow> timeline_;
  std::function<void(const TimelineRow&)> sink_;
  std::function<void(double, const std::string&)> noteSink_;
  void note(double t, const std::string& text);
  bool music_ = false;
  float preSafetyDelay_ = 60;
  bool flick_ = false;
};

}  // namespace fxa
