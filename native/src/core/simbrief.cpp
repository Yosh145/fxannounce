#include "core/simbrief.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/strings.h"

namespace fxa {

namespace {

constexpr double kDay = 86400.0;
constexpr double kPi = 3.14159265358979323846;

// digits only else nullopt
std::optional<long long> parseWhole(const std::string& text) {
  const std::string t(trim(text));
  if (t.empty() || t.size() > 18) return std::nullopt;
  for (char c : t)
    if (c < '0' || c > '9') return std::nullopt;
  errno = 0;
  const long long v = std::strtoll(t.c_str(), nullptr, 10);
  if (errno != 0) return std::nullopt;
  return v;
}

}  // namespace

std::optional<Ofp> parseOfp(const OfpText& t, std::string* why) {
  auto fail = [&](const char* reason) -> std::optional<Ofp> {
    if (why) *why = reason;
    return std::nullopt;
  };
  if (t.seqno <= 0) return fail("simbrief_hub has no OFP yet");
  const auto etd = parseWhole(t.estOut);
  if (!etd || *etd < 946684800)
    return fail("the OFP has no usable ETD (est_out)");  // pre 2000 garbage
  Ofp o;
  o.seqno = t.seqno;
  o.stale = t.stale;
  o.estOut = *etd;
  const std::string airline = toUpper(trim(t.icaoAirline));
  const std::string number = toUpper(trim(t.flightNumber));
  if (!airline.empty() && !number.empty()) o.callsign = airline + number;
  o.origin = toUpper(trim(t.origin));
  o.destination = toUpper(trim(t.destination));
  o.aircraftIcao = toUpper(trim(t.aircraftIcao));
  if (const auto p = parseWhole(t.paxCount); p && *p <= 2000)
    o.pax = static_cast<int>(*p);
  if (const auto m = parseWhole(t.maxPassengers); m && *m > 0 && *m <= 2000)
    o.seats = static_cast<int>(*m);
  if (why) why->clear();
  return o;
}

double distanceNm(double lat1, double lon1, double lat2, double lon2) {
  const double r = kPi / 180.0;
  const double dLat = (lat2 - lat1) * r, dLon = (lon2 - lon1) * r;
  const double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
                   std::cos(lat1 * r) * std::cos(lat2 * r) *
                       std::sin(dLon / 2) * std::sin(dLon / 2);
  return 2.0 * std::atan2(std::sqrt(a), std::sqrt(std::max(0.0, 1.0 - a))) *
         3440.065;
}

std::string checkOfp(const Ofp& ofp, bool originFound, double originLat,
                     double originLon, double acLat, double acLon,
                     const Settings& set) {
  if (ofp.origin.empty()) return "the OFP has no origin";
  if (!originFound)
    return "origin " + ofp.origin + " is not in X-Plane's airport data";
  const double d = distanceNm(originLat, originLon, acLat, acLon);
  if (!std::isfinite(d) || d > set.ofpOriginNm) {
    char buf[96];
    std::snprintf(buf, sizeof(buf), "origin %s is %.0f NM away",
                  ofp.origin.c_str(), std::isfinite(d) ? d : 0.0);
    return buf;
  }
  return {};
}

void OfpTracker::update(const std::optional<Ofp>& ofp, const OfpPlace& origin,
                        const OfpPlace& dest, double lat, double lon,
                        bool onGround, bool tookOff, const Settings& set) {
  if (!tookOff && latched_) {
    // latched ofp got flown new flight
    flownSeqno_ = latched_->ofp.seqno;
    latched_.reset();
  }
  usable_ = currentLatched_ = false;
  why_.clear();
  next_.clear();
  auto dist = [&](const OfpPlace& p) {
    return p.found ? distanceNm(lat, lon, p.lat, p.lon) : -1.0;
  };

  if (latched_) {
    Latched& l = *latched_;
    distOrigin_ = dist(l.origin);
    distDest_ = dist(l.dest);
    const double near = set.ofpOriginNm;
    if (onGround && distDest_ >= 0 && distDest_ <= near) {
      stage_ = Stage::Arrived;
    } else if (onGround && distOrigin_ >= 0 && distOrigin_ <= near) {
      stage_ = Stage::AtOrigin;  // back at origin passes again
    } else if (!onGround) {
      if (distOrigin_ >= 0) l.maxOrigin = std::max(l.maxOrigin, distOrigin_);
      if (distDest_ >= 0) l.minDest = std::min(l.minDest, distDest_);
      // returning needs both closer to origin than max and farther from dest
      // than min a departure looping over the field still moves toward dest
      const bool back = distOrigin_ >= 0 && distDest_ >= 0 &&
                        distOrigin_ <= l.maxOrigin - set.ofpReturnNm &&
                        distDest_ >= l.minDest + set.ofpReturnNm;
      stage_ = back ? Stage::Returning : Stage::EnRoute;
    }
    if (ofp && ofp->seqno == l.ofp.seqno) {
      currentLatched_ = true;
      usable_ = stage_ == Stage::AtOrigin;
    } else if (ofp) {
      // newer ofp next leg
      next_ = (ofp->callsign.empty() ? std::string("OFP") : ofp->callsign) +
              " " + ofp->origin + "-" + ofp->destination;
      if (onGround) {
        why_ =
            checkOfp(*ofp, origin.found, origin.lat, origin.lon, lat, lon, set);
        usable_ = why_.empty();
      } else {
        why_ = "next leg, checked at " + ofp->origin;
      }
    }
    return;
  }

  distOrigin_ = distDest_ = -1;
  if (!ofp) {
    stage_ = Stage::NoOfp;
    return;
  }
  why_ = checkOfp(*ofp, origin.found, origin.lat, origin.lon, lat, lon, set);
  usable_ = why_.empty();
  stage_ = usable_ ? Stage::AtOrigin : Stage::NotHere;
  if (!usable_ && ofp->seqno == flownSeqno_)
    why_ += " (the OFP of the flight just flown: load the next leg's)";
  if (tookOff && usable_) {
    Latched l;
    l.ofp = *ofp;
    l.origin = origin;
    l.dest = dest;
    latched_ = l;
    stage_ = Stage::EnRoute;
    usable_ = false;
    currentLatched_ = true;
  }
}

std::string OfpTracker::status() const {
  if (!latched_) return {};
  const Latched& l = *latched_;
  std::string s =
      (l.ofp.callsign.empty() ? std::string("OFP") : l.ofp.callsign) + " " +
      l.ofp.origin + "-" + l.ofp.destination + ": ";
  char nm[32] = "";
  switch (stage_) {
    case Stage::Arrived:
      s += "arrived at " + l.ofp.destination;
      break;
    case Stage::AtOrigin:
      s += "back at " + l.ofp.origin;
      break;
    case Stage::Returning:
      if (distOrigin_ >= 0)
        std::snprintf(nm, sizeof(nm), ", %.0f NM", distOrigin_);
      s += "returning to " + l.ofp.origin + nm;
      break;
    default:
      if (distDest_ >= 0) std::snprintf(nm, sizeof(nm), ", %.0f NM", distDest_);
      s += "en route to " + l.ofp.destination + nm;
      break;
  }
  if (!next_.empty()) s += "; next OFP " + next_ + (usable_ ? " (ready)" : "");
  return s;
}

double secondOfDay(double s) {
  if (!std::isfinite(s)) return 0;
  double r = std::fmod(s, kDay);
  if (r < 0) r += kDay;
  return r;
}

double secondsUntil(double atSec, double nowSec) {
  double d = secondOfDay(atSec) - secondOfDay(nowSec);
  if (d > kDay / 2) d -= kDay;
  if (d <= -kDay / 2) d += kDay;
  return d;
}

LeadRange leadRange(Family f, const Settings& set) {
  LeadRange r;
  switch (f) {
    case Family::A319:
      r = {set.a319LeadMin * 60.0, set.a319LeadMax * 60.0, set.a319Seats};
      break;
    case Family::A321:
      r = {set.a321LeadMin * 60.0, set.a321LeadMax * 60.0, set.a321Seats};
      break;
    default:
      r = {set.a320LeadMin * 60.0, set.a320LeadMax * 60.0, set.a320Seats};
      break;
  }
  if (r.maxS < r.minS) std::swap(r.minS, r.maxS);
  r.seats = std::max(1, r.seats);
  return r;
}

double boardingLead(Family f, int pax, int ofpSeats, const Settings& set,
                    double jitterS) {
  const LeadRange r = leadRange(f, set);
  const int seats = ofpSeats > 0 ? ofpSeats : r.seats;
  const double load =
      std::clamp(static_cast<double>(std::max(0, pax)) / seats, 0.0, 1.0);
  return std::max(0.0, r.minS + (r.maxS - r.minS) * load + jitterS);
}

std::string zuluLabel(double sec) {
  const int m = static_cast<int>(secondOfDay(sec) / 60.0);
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d:%02dZ", (m / 60) % 24, m % 60);
  return buf;
}

BoardingPlan makeBoardingPlan(const Ofp& ofp, Family f, int pax,
                              const Settings& set, double jitterS) {
  BoardingPlan p;
  p.valid = true;
  p.seqno = ofp.seqno;
  p.etdSec = secondOfDay(static_cast<double>(ofp.estOut));
  // toliss pax count wins over the ofp
  const int people = pax > 0 ? pax : std::max(0, ofp.pax);
  p.pax = people;
  const double lead = boardingLead(f, people, ofp.seats, set, jitterS);
  p.boardingAtSec = secondOfDay(p.etdSec - lead);
  char buf[64];
  std::snprintf(buf, sizeof(buf), ", lead %.0f min for %d pax", lead / 60.0,
                people);
  p.summary =
      (ofp.callsign.empty() ? std::string("OFP") : ofp.callsign) + " " +
      ofp.origin + "-" + ofp.destination + ", ETD " + zuluLabel(p.etdSec) +
      ", boarding " + zuluLabel(p.boardingAtSec) + buf +
      (ofp.stale ? " (simbrief_hub: last download failed, older plan)" : "");
  return p;
}

}  // namespace fxa
