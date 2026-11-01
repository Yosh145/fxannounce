// Installed Airline Packs: folder discovery and file scan (VP_SPEC.md § 1.1, § 1.2). Pure filesystem code; runs on
// the scan worker.
#pragma once

#include "core/sound_file.h"

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace fxa {

struct Pack {
    std::string name;                 // folder name: "DLH", "Default", "UALES"
    std::string root;                 // full path, UTF-8
    std::string wrapper;              // wrapper folder name if found inside one ("DLH OLD PA SYSTEM")
    bool mappingOnly = false;         // not named like a pack id: reachable only via a livery mapping
    std::vector<SoundFile> files;     // playable and custom sounds
    std::vector<std::string> warnings;
    int maxVariant = 0;

    bool isDefault() const;
    int countOf(AnnType t) const;
};

struct PackLibrary {
    std::vector<Pack> packs;          // sorted by name
    std::vector<std::string> warnings;
    std::vector<std::string> roots;

    // Case-insensitive lookup by pack name.
    const Pack* find(std::string_view name) const;
    const Pack* defaultPack() const { return find("Default"); }
    size_t soundCount() const;
};

// Pack folder names (VP_SPEC § 1.1): 2-4 letters/digits, or "Default".
bool isPackIdName(std::string_view folderName);

// Scans every root. A missing root is not an error (logged as a warning). `cancel` may be null.
PackLibrary scanPacks(const std::vector<std::string>& roots, const std::atomic<bool>* cancel = nullptr);

} // namespace fxa
