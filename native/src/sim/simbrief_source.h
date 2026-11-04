// reads the ofp simbrief_hub publishes sbh/ origin airport lookup hub is
// optional and can load after us main thread
#pragma once

#include <string>
#include <vector>

#include "core/simbrief.h"
#include "sim/dataref.h"

namespace fxa {

class SimbriefSource {
 public:
  SimbriefSource();
  SimbriefSource(const SimbriefSource&) = delete;  // refs_ points into this
  SimbriefSource& operator=(const SimbriefSource&) = delete;

  // retries the lookup every 10s till the hub shows up rereads on sbh/seqno
  // change true when text changed
  bool poll(double now);
  // plane load
  void reset();
  bool installed() const { return installed_; }
  const OfpText& text() const { return text_; }

  // nearest airport w exactly this icao
  static bool findAirport(const std::string& icao, double nearLat,
                          double nearLon, double& lat, double& lon);

 private:
  DataRef seqno_{"sbh/seqno"}, stale_{"sbh/stale"}, estOut_{"sbh/est_out"},
      airline_{"sbh/icao_airline"}, flightNo_{"sbh/flight_number"},
      origin_{"sbh/origin"}, dest_{"sbh/destination"}, pax_{"sbh/pax_count"},
      maxPax_{"sbh/max_passengers"}, acIcao_{"sbh/aircraft_icao"};
  std::vector<DataRef*> refs_;
  double nextResolve_ = -1;
  bool installed_ = false;
  int seenSeqno_ = -1;
  int seenStale_ = -1;
  OfpText text_;
};

}  // namespace fxa
