// Reads the SimBrief OFP that the simbrief_hub plugin publishes (sbh/* datarefs, docs/TOLISS_DATAREFS.md) and looks
// up its origin airport. Read-only; simbrief_hub is optional and may load after FXAnnounce. Main thread only.
#pragma once

#include "core/simbrief.h"
#include "sim/dataref.h"

#include <string>
#include <vector>

namespace fxa {

class SimbriefSource {
public:
    SimbriefSource();
    SimbriefSource(const SimbriefSource&) = delete;   // refs_ points into this object
    SimbriefSource& operator=(const SimbriefSource&) = delete;

    // Call every tick with a clock in seconds. Looks the datarefs up again every 10 s until simbrief_hub is there, and
    // re-reads the OFP whenever sbh/seqno changes. Returns true when the text changed.
    bool poll(double now);
    // Forget the cached OFP and look the datarefs up again on the next poll (plane load).
    void reset();
    bool installed() const { return installed_; }
    const OfpText& text() const { return text_; }

    // The airport with exactly this ICAO code nearest to (lat, lon), from X-Plane's navigation data.
    static bool findAirport(const std::string& icao, double nearLat, double nearLon, double& lat, double& lon);

private:
    DataRef seqno_{ "sbh/seqno" }, stale_{ "sbh/stale" }, estOut_{ "sbh/est_out" }, airline_{ "sbh/icao_airline" },
        flightNo_{ "sbh/flight_number" }, origin_{ "sbh/origin" }, dest_{ "sbh/destination" }, pax_{ "sbh/pax_count" },
        maxPax_{ "sbh/max_passengers" }, acIcao_{ "sbh/aircraft_icao" };
    std::vector<DataRef*> refs_;
    double nextResolve_ = -1;
    bool installed_ = false;
    int seenSeqno_ = -1;
    int seenStale_ = -1;
    OfpText text_;
};

} // namespace fxa
