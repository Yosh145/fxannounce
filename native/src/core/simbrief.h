// simbrief ofp from simbrief_hub the origin check and boarding
// schedule 4.1a 4.2a sim/simbrief_source does the dataref side
#pragma once

#include <optional>
#include <string>

#include "core/aircraft.h"
#include "core/config.h"

namespace fxa {

// sbh/ fields as text
struct OfpText {
  int seqno = 0;       // 0 nothing downloaded yet
  bool stale = false;  // last download failed
  std::string estOut;  // utc epoch
  std::string icaoAirline, flightNumber, origin, destination, aircraftIcao;
  std::string paxCount, maxPassengers;
};

struct Ofp {
  int seqno = 0;
  bool stale = false;
  long long estOut = 0;  // etd utc epoch
  std::string callsign;  // eg dlh400 can be empty
  std::string origin, destination, aircraftIcao;
  int pax = -1;  // -1 not in the ofp
  int seats = -1;
};

std::optional<Ofp> parseOfp(const OfpText& t, std::string* why = nullptr);

// nm
double distanceNm(double lat1, double lon1, double lat2, double lon2);

// empty usable here originfound false xp doesnt know the code
std::string checkOfp(const Ofp& ofp, bool originFound, double originLat,
                     double originLon, double acLat, double acLon,
                     const Settings& set);

struct OfpPlace {
  bool found = false;
  double lat = 0, lon = 0;
};

// ofp over the flight 4.2a checked at origin before takeoff latched at takeoff
// and tracked by distance en route returning arrived newer ofp in flight next
// leg checked once on the ground
class OfpTracker {
 public:
  enum class Stage { NoOfp, AtOrigin, NotHere, EnRoute, Returning, Arrived };

  // tookoff comes from the purser
  void update(const std::optional<Ofp>& ofp, const OfpPlace& origin,
              const OfpPlace& dest, double lat, double lon, bool onGround,
              bool tookOff, const Settings& set);
  void reset() { *this = OfpTracker{}; }

  bool usable() const { return usable_; }
  bool currentIsLatched() const { return currentLatched_; }
  bool latched() const { return latched_.has_value(); }
  Stage stage() const { return stage_; }
  const std::string& why() const { return why_; }
  // status line for the latched flight empty otherwise
  std::string status() const;

 private:
  struct Latched {
    Ofp ofp;
    OfpPlace origin, dest;
    double maxOrigin = 0, minDest = 1e12;
  };
  std::optional<Latched> latched_;
  int flownSeqno_ = -1;
  Stage stage_ = Stage::NoOfp;
  bool usable_ = false, currentLatched_ = false;
  std::string why_, next_;
  double distOrigin_ = -1, distDest_ = -1;
};

// 0 to 86400
double secondOfDay(double s);
// wrapped to -12h 12h xp has no year so compare time of day
double secondsUntil(double atSec, double nowSec);

struct LeadRange {
  double minS = 0, maxS = 0;  // empty vs full cabin
  int seats = 1;
};
LeadRange leadRange(Family f, const Settings& set);
// min max min x pax/seats jitter never negative
double boardingLead(Family f, int pax, int ofpSeats, const Settings& set,
                    double jitterS);

// 4.1a
struct BoardingPlan {
  bool valid = false;
  double boardingAtSec = 0;  // utc sec of day
  double etdSec = 0;
  int seqno = 0;
  int pax = 0;          // toliss nopax else the ofp
  std::string summary;  // or why theres no plan
};
BoardingPlan makeBoardingPlan(const Ofp& ofp, Family f, int pax,
                              const Settings& set, double jitterS);

std::string zuluLabel(double sec);

}  // namespace fxa
