// picks the file fenix tag rules 3.2 to 3.4
#pragma once

#include <random>
#include <string>
#include <vector>

#include "core/aircraft.h"
#include "core/pack_library.h"

namespace fxa {

using Rng = std::mt19937;

struct SelectContext {
  AircraftType aircraft;
  bool refueling = false;
  TimeOfDay time = TimeOfDay::Morning;
  double localHour = -1;  // 0-24 for d-28 nearness, <0 middle of time
  Phase phase = Phase::PreFlight;
  bool overwater = false;
  std::vector<std::string> customTags;  // livery satcom
  int variant = 0;
  CallContext context = CallContext::Any;
};

struct Candidate {
  const SoundFile* file = nullptr;
  bool eligible = false;
  bool variantPool = false;  // in the variant used for this call
  float score = 0;
  std::string why;  // eg a320 ok evening ok
};

struct Selection {
  const SoundFile* file = nullptr;  // nullptr skip it
  float score = 0;
  int variant = 0;                      // variant the file came from
  bool variantFallback = false;         // session variant had nothing that fits
  TimeOfDay time = TimeOfDay::Morning;  // time of day the choice used
  bool timeFallback = false;          // d-28 nothing for ctx.time, nearest used
  std::vector<Candidate> candidates;  // all files of the type for the explainer
};

// other times of day nearest first d-28
std::vector<TimeOfDay> nearestTimesOfDay(TimeOfDay now, double localHour);

// lastpath avoided on ties
Selection selectSound(const Pack& pack, AnnType type, const SelectContext& ctx,
                      Rng& rng, const std::string& lastPath = {});

// random pick from pack.variantChoices() 3.4 d-27
int pickVariant(const Pack& pack, Rng& rng);

}  // namespace fxa
