// type tag tag display name.ogg vp_spec 1.2 3.1
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/announcement.h"

namespace fxa {

struct SoundFile {
  std::string path;
  std::string relPath;  // separated
  std::string typeName;
  std::optional<AnnType>
      type;  // nullopt custom fap sound purser never plays it
  CallContext context = CallContext::Any;
  bool legacy = false;
  std::string displayName;

  // tags 3.1
  std::vector<std::string> aircraft;  // upper case
  bool refuel = false;
  std::vector<TimeOfDay> times;
  std::vector<Phase> phases;
  std::optional<Mode> mode;
  bool overwater = false;
  std::vector<int> variants;  // empty variant 0
  std::vector<std::string> custom;
  std::vector<std::string> rawTags;
  std::vector<std::string> warnings;  // stray text empty tags

  Mode effectiveMode() const {
    return mode ? *mode : (type ? defaultMode(*type) : Mode::Manual);
  }
  bool hasVariant(int v) const;
};

// extension optional nullopt if theres no type name
std::optional<SoundFile> parseSoundFileName(std::string_view fileName);

// rebuilt label for ui/logs
std::string describeTags(const SoundFile& f);

}  // namespace fxa
