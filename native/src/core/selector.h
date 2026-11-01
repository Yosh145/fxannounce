// Which file plays for an announcement: the Fenix tag rules (VP_SPEC.md § 3.2–3.4).
#pragma once

#include "core/aircraft.h"
#include "core/pack_library.h"

#include <random>
#include <string>
#include <vector>

namespace fxa {

using Rng = std::mt19937;

struct SelectContext {
    AircraftType aircraft;
    bool refueling = false;
    TimeOfDay time = TimeOfDay::Morning;
    Phase phase = Phase::PreFlight;
    bool overwater = false;
    std::vector<std::string> customTags;   // livery tags plus SatCom tags
    int variant = 0;                       // session variant
    CallContext context = CallContext::Any;
};

struct Candidate {
    const SoundFile* file = nullptr;
    bool eligible = false;
    bool variantPool = false;   // in the pool actually used (session variant, or variant 0 fallback)
    float score = 0;
    std::string why;            // "A320 ok, Evening ok, Refueling: not refuelling"
};

struct Selection {
    const SoundFile* file = nullptr;      // nullptr: nothing eligible, the announcement is skipped
    float score = 0;
    bool fellBackToVariant0 = false;
    std::vector<Candidate> candidates;    // every file of the type, for the explainer
};

// Picks the best file of `type` in `pack`. `lastPath` is the file played last for this type (avoided on ties).
Selection selectSound(const Pack& pack, AnnType type, const SelectContext& ctx, Rng& rng, const std::string& lastPath = {});

// Session variant in 0..pack.maxVariant (VP_SPEC § 3.4).
int pickVariant(const Pack& pack, Rng& rng);

} // namespace fxa
