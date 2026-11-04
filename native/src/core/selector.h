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
  Phase phase = Phase::PreFlight;
  bool overwater = false;
  std::vector<std::string> customTags;  // livery satcom
  int variant = 0;
  CallContext context = CallContext::Any;
};

struct Candidate {
  const SoundFile* file = nullptr;
  bool eligible = false;
  bool variantPool = false;  // session variant or variant 0 fallback
  float score = 0;
  std::string why;  // eg a320 ok evening ok
};

struct Selection {
  const SoundFile* file = nullptr;  // nullptr skip it
  float score = 0;
  bool fellBackToVariant0 = false;
  std::vector<Candidate> candidates;  // all files of the type for the explainer
};

// lastpath avoided on ties
Selection selectSound(const Pack& pack, AnnType type, const SelectContext& ctx,
                      Rng& rng, const std::string& lastPath = {});

// 0 to maxvariant 3.4
int pickVariant(const Pack& pack, Rng& rng);

}  // namespace fxa
