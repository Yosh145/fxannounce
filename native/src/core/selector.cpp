#include "core/selector.h"

#include <algorithm>
#include <cmath>

#include "core/strings.h"

namespace fxa {

namespace {

std::string variantLabel(const SoundFile& f) {
  if (f.variants.empty()) return "0";
  std::string out;
  for (int v : f.variants) out += (out.empty() ? "" : "/") + std::to_string(v);
  return out;
}

bool hasTag(const std::vector<std::string>& tags, const std::string& t) {
  return std::any_of(tags.begin(), tags.end(),
                     [&](const std::string& x) { return iequals(x, t); });
}

// variant is the callers problem
void evaluate(Candidate& c, const SelectContext& ctx) {
  const SoundFile& f = *c.file;
  std::vector<std::string> notes;
  bool ok = true;
  float score = 0;
  auto fail = [&](std::string note) {
    ok = false;
    notes.push_back(std::move(note));
  };

  if (f.context != CallContext::Any) {
    if (ctx.context != CallContext::Any && f.context != ctx.context)
      fail(std::string("for ") + contextName(f.context) + ", not " +
           contextName(ctx.context));
    else {
      score += 1;
      notes.push_back(std::string(contextName(f.context)) + " call");
    }
  }
  if (!f.aircraft.empty()) {
    bool any = false, specific = false;
    for (const std::string& a : f.aircraft) {
      if (hasTag(ctx.aircraft.tags, a)) {
        any = true;
        if (iequals(a, ctx.aircraft.specific)) specific = true;
      }
    }
    if (!any)
      fail("aircraft " + join(f.aircraft, "/") + " is not " +
           ctx.aircraft.label);
    else {
      score += specific ? 1.5f : 1.0f;
      notes.push_back("aircraft ok" + std::string(specific ? " (exact)" : ""));
    }
  }
  if (f.refuel) {
    if (!ctx.refueling)
      fail("not refuelling");
    else {
      score += 1;
      notes.push_back("refuelling");
    }
  }
  if (!f.times.empty()) {
    if (std::find(f.times.begin(), f.times.end(), ctx.time) == f.times.end())
      fail(std::string("not ") +
           (f.times.size() == 1 ? timeOfDayName(f.times[0])
                                : "the right time of day") +
           " (now " + timeOfDayName(ctx.time) + ")");
    else {
      score += 1;
      notes.push_back(timeOfDayName(ctx.time));
    }
  }
  if (!f.phases.empty()) {
    if (std::find(f.phases.begin(), f.phases.end(), ctx.phase) ==
        f.phases.end())
      fail(std::string("phase is ") + phaseName(ctx.phase));
    else {
      score += 1;
      notes.push_back(phaseName(ctx.phase));
    }
  }
  if (f.overwater) {
    if (!ctx.overwater)
      fail("not an overwater flight");
    else {
      score += 1;
      notes.push_back("overwater");
    }
  }
  for (const std::string& t : f.custom) {
    if (!hasTag(ctx.customTags, t))
      fail("livery lacks [" + t + "]");
    else {
      score += 1;
      notes.push_back("[" + t + "]");
    }
  }
  c.eligible = ok;
  c.score = ok ? score : 0;
  c.why = notes.empty() ? "no tags" : join(notes, ", ");
}

Selection selectExact(const Pack& pack, AnnType type, const SelectContext& ctx,
                      Rng& rng, const std::string& lastPath) {
  Selection sel;
  sel.time = ctx.time;
  for (const SoundFile& f : pack.files) {
    if (f.type != type) continue;
    Candidate c;
    c.file = &f;
    evaluate(c, ctx);
    sel.candidates.push_back(std::move(c));
  }
  auto poolHas = [&](int v) {
    return std::any_of(sel.candidates.begin(), sel.candidates.end(),
                       [&](const Candidate& c) {
                         return c.eligible && c.file->hasVariant(v);
                       });
  };
  int pool = ctx.variant;
  if (!poolHas(pool)) {
    if (pool != 0 && poolHas(0)) {
      pool = 0;
    } else {
      std::vector<int> others;
      for (const Candidate& c : sel.candidates) {
        if (!c.eligible) continue;
        for (int v : c.file->variants)
          if (v != ctx.variant &&
              std::find(others.begin(), others.end(), v) == others.end())
            others.push_back(v);
      }
      if (!others.empty()) {
        std::sort(others.begin(), others.end());
        std::uniform_int_distribution<size_t> d(0, others.size() - 1);
        pool = others[d(rng)];
      }
    }
  }
  sel.variant = pool;
  sel.variantFallback = pool != ctx.variant;
  float best = -1;
  std::vector<const Candidate*> top;
  for (Candidate& c : sel.candidates) {
    if (!c.file->hasVariant(pool)) {
      if (c.eligible)
        c.why += ", variant " + variantLabel(*c.file) + " not in use";
      c.eligible = false;
      continue;
    }
    c.variantPool = true;
    if (!c.eligible) continue;
    if (c.score > best + 1e-4f) {
      best = c.score;
      top.clear();
    }
    if (c.score > best - 1e-4f) top.push_back(&c);
  }
  if (top.empty()) return sel;
  // random but not the last one if theres a choice
  if (top.size() > 1 && !lastPath.empty()) {
    top.erase(std::remove_if(top.begin(), top.end(),
                             [&](const Candidate* c) {
                               return c->file->path == lastPath;
                             }),
              top.end());
  }
  std::uniform_int_distribution<size_t> pick(0, top.size() - 1);
  const Candidate* chosen = top[pick(rng)];
  sel.file = chosen->file;
  sel.score = chosen->score;
  return sel;
}

}  // namespace

std::vector<TimeOfDay> nearestTimesOfDay(TimeOfDay now, double localHour) {
  struct Span {
    TimeOfDay t;
    double from, to;
  };
  static const Span spans[] = {{TimeOfDay::Morning, 6, 12},
                               {TimeOfDay::Afternoon, 12, 18},
                               {TimeOfDay::Evening, 18, 24},
                               {TimeOfDay::Night, 0, 6}};
  double h = localHour;
  if (!(h >= 0) || !std::isfinite(h)) {
    for (const Span& s : spans)
      if (s.t == now) h = (s.from + s.to) / 2;
  }
  h = std::fmod(h, 24.0);
  struct Ranked {
    TimeOfDay t;
    double dist;
    bool coming;
  };
  std::vector<Ranked> r;
  for (const Span& s : spans) {
    if (s.t == now) continue;
    const double ahead = std::fmod(s.from - h + 48.0, 24.0);
    const double back = std::fmod(h - s.to + 48.0, 24.0);
    r.push_back({s.t, std::min(ahead, back), ahead <= back});
  }
  std::stable_sort(r.begin(), r.end(), [](const Ranked& a, const Ranked& b) {
    if (std::fabs(a.dist - b.dist) > 1e-9) return a.dist < b.dist;
    return a.coming && !b.coming;
  });
  std::vector<TimeOfDay> out;
  for (const Ranked& x : r) out.push_back(x.t);
  return out;
}

Selection selectSound(const Pack& pack, AnnType type, const SelectContext& ctx,
                      Rng& rng, const std::string& lastPath) {
  Selection sel = selectExact(pack, type, ctx, rng, lastPath);
  if (sel.file) return sel;
  const bool timed =
      std::any_of(sel.candidates.begin(), sel.candidates.end(),
                  [](const Candidate& c) { return !c.file->times.empty(); });
  if (!timed) return sel;
  for (TimeOfDay t : nearestTimesOfDay(ctx.time, ctx.localHour)) {
    SelectContext near = ctx;
    near.time = t;
    Selection alt = selectExact(pack, type, near, rng, lastPath);
    if (!alt.file) continue;
    alt.timeFallback = true;
    return alt;
  }
  return sel;
}

int pickVariant(const Pack& pack, Rng& rng) {
  const std::vector<int> choices = pack.variantChoices();
  if (choices.size() == 1) return choices[0];
  std::uniform_int_distribution<size_t> d(0, choices.size() - 1);
  return choices[d(rng)];
}

}  // namespace fxa
