// The SimBrief OFP as simbrief_hub publishes it, its check, and the boarding schedule (VP_SPEC.md § 4.1a, § 4.2a).
// Pure C++: sim/simbrief_source.cpp reads the sbh/* datarefs into OfpText and looks up the origin airport.
#pragma once

#include "core/aircraft.h"
#include "core/config.h"

#include <optional>
#include <string>

namespace fxa {

// The sbh/* fields FXAnnounce reads, as text (docs/TOLISS_DATAREFS.md, simbrief_hub section).
struct OfpText {
    int seqno = 0;              // sbh/seqno: 0 = nothing downloaded yet
    bool stale = false;         // sbh/stale: the last download failed
    std::string estOut;         // UTC epoch seconds
    std::string icaoAirline, flightNumber, origin, destination, aircraftIcao;
    std::string paxCount, maxPassengers;
};

struct Ofp {
    int seqno = 0;
    bool stale = false;
    long long estOut = 0;       // ETD, UTC epoch seconds
    std::string callsign;       // icao_airline + flight_number ("DLH400"), may be ""
    std::string origin, destination, aircraftIcao;
    int pax = -1;               // -1: not in the OFP
    int seats = -1;
};

// nullopt when there is no OFP or its ETD is unusable; `why` (if given) says which.
std::optional<Ofp> parseOfp(const OfpText& t, std::string* why = nullptr);

// Great-circle distance, NM.
double distanceNm(double lat1, double lon1, double lat2, double lon2);

// "" when the OFP can be used here, otherwise the reason ("origin EDDF is 160 NM away"). `originFound` false means
// X-Plane has no airport with the OFP's origin code.
std::string checkOfp(const Ofp& ofp, bool originFound, double originLat, double originLon, double acLat, double acLon,
                     const Settings& set);

// Seconds of the UTC day, in [0, 86400).
double secondOfDay(double s);
// `atSec` minus `nowSec` (both UTC seconds of the day), wrapped to (-12 h, +12 h]. X-Plane has no year, so the
// boarding time is compared by time of day.
double secondsUntil(double atSec, double nowSec);

struct LeadRange {
    double minS = 0, maxS = 0;  // seconds before the ETD for an empty / a full cabin
    int seats = 1;              // when the OFP has none
};
LeadRange leadRange(Family f, const Settings& set);
// min + (max - min) x clamp(pax / seats, 0, 1) + jitterS (rolled once per flight by the caller). Seats: the OFP's
// when > 0, else the type default. Never negative.
double boardingLead(Family f, int pax, int ofpSeats, const Settings& set, double jitterS);

// What the purser follows for the boarding start (VP_SPEC § 4.1a).
struct BoardingPlan {
    bool valid = false;         // a checked OFP: boarding at boardingAtSec
    double boardingAtSec = 0;   // UTC second of the day
    double etdSec = 0;          // UTC second of the day
    int seqno = 0;              // the OFP it came from
    std::string summary;        // "DLH400 EDDM-EGLL, ETD 14:35Z, boarding 13:58Z" or why there is no plan
};
BoardingPlan makeBoardingPlan(const Ofp& ofp, Family f, int pax, const Settings& set, double jitterS);

// "14:35Z" for a UTC second of the day.
std::string zuluLabel(double sec);

} // namespace fxa
