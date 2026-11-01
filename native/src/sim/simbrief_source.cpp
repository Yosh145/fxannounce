#include "sim/simbrief_source.h"

#include "core/log.h"
#include "core/strings.h"

#include <XPLMNavigation.h>

namespace fxa {

namespace {
constexpr double kResolveEvery = 10.0;   // s between look-ups while simbrief_hub is missing
}

SimbriefSource::SimbriefSource()
{
    refs_ = { &seqno_, &stale_, &estOut_, &airline_, &flightNo_, &origin_, &dest_, &pax_, &maxPax_, &acIcao_ };
}

void SimbriefSource::reset()
{
    nextResolve_ = -1;
    installed_ = false;
    seenSeqno_ = -1;
    seenStale_ = -1;
    text_ = OfpText{};
}

bool SimbriefSource::poll(double now)
{
    if (!installed_ || !seqno_.good()) {
        if (installed_) {
            logInfo("simbrief_hub datarefs went away");
            installed_ = false;
            text_ = OfpText{};
            seenSeqno_ = seenStale_ = -1;
            return true;
        }
        if (nextResolve_ >= 0 && now < nextResolve_) return false;
        nextResolve_ = now + kResolveEvery;
        if (!seqno_.resolve()) return false;
        for (DataRef* r : refs_) r->resolve();
        installed_ = true;
        logInfo("simbrief_hub found: reading its SimBrief OFP");
    }
    const int seq = seqno_.getInt().value_or(0);
    const int stale = stale_.getInt().value_or(0);
    if (seq == seenSeqno_ && stale == seenStale_) return false;
    seenSeqno_ = seq;
    seenStale_ = stale;
    OfpText t;
    t.seqno = seq;
    t.stale = stale != 0;
    if (seq > 0) {
        t.estOut = estOut_.getString(32);
        t.icaoAirline = airline_.getString(16);
        t.flightNumber = flightNo_.getString(16);
        t.origin = origin_.getString(16);
        t.destination = dest_.getString(16);
        t.paxCount = pax_.getString(16);
        t.maxPassengers = maxPax_.getString(16);
        t.aircraftIcao = acIcao_.getString(16);
    }
    text_ = t;
    return true;
}

bool SimbriefSource::findAirport(const std::string& icao, double nearLat, double nearLon, double& lat, double& lon)
{
    const std::string id = toUpper(trim(icao));
    if (id.size() < 3 || id.size() > 7) return false;
    float la = static_cast<float>(nearLat), lo = static_cast<float>(nearLon);
    // The ID is a fragment match: the nearest airport whose ID contains it, then an exact check.
    const XPLMNavRef ref = XPLMFindNavAid(nullptr, id.c_str(), &la, &lo, nullptr, xplm_Nav_Airport);
    if (ref == XPLM_NAV_NOT_FOUND) return false;
    float outLat = 0, outLon = 0;
    char outId[32] = {};
    XPLMGetNavAidInfo(ref, nullptr, &outLat, &outLon, nullptr, nullptr, nullptr, outId, nullptr, nullptr);
    outId[sizeof(outId) - 1] = '\0';
    if (!iequals(outId, id)) return false;
    lat = outLat;
    lon = outLon;
    return true;
}

} // namespace fxa
