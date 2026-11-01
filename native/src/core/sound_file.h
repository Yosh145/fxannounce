// One sound file of an Airline Pack, parsed from its name: Type[tag]...[tag]~Display Name.ogg (VP_SPEC.md § 1.2,
// § 3.1).
#pragma once

#include "core/announcement.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fxa {

struct SoundFile {
    std::string path;            // full path, UTF-8
    std::string relPath;         // relative to the pack folder, '/'-separated
    std::string typeName;        // as written in the file name
    std::optional<AnnType> type; // nullopt: a custom (FAP-only) sound, never played by the purser
    CallContext context = CallContext::Any;
    bool legacy = false;
    std::string displayName;     // after '~', or the file name without extension

    // Tags by category (VP_SPEC § 3.1)
    std::vector<std::string> aircraft;   // upper case: A320, A21N, B738...
    bool refuel = false;
    std::vector<TimeOfDay> times;
    std::vector<Phase> phases;
    std::optional<Mode> mode;
    bool overwater = false;
    std::vector<int> variants;           // empty = variant 0
    std::vector<std::string> custom;     // as written
    std::vector<std::string> rawTags;    // every tag as written, in order
    std::vector<std::string> warnings;   // parse oddities (stray text, empty tags)

    Mode effectiveMode() const { return mode ? *mode : (type ? defaultMode(*type) : Mode::Manual); }
    bool hasVariant(int v) const;
};

// Parses a file name without folder (extension optional). Returns nullopt if no type name can be read.
std::optional<SoundFile> parseSoundFileName(std::string_view fileName);

// "AfterLanding[Afternoon]" style label rebuilt from the parsed parts (for the UI and logs).
std::string describeTags(const SoundFile& f);

} // namespace fxa
