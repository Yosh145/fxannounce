// pack discovery scan 1.1 1.2 filesystem only runs on the scan worker
#pragma once

#include <atomic>
#include <string>
#include <string_view>
#include <vector>

#include "core/sound_file.h"

namespace fxa {

struct Pack {
  std::string name;  // dlh default uales
  std::string root;
  std::string wrapper;       // eg dlh old pa system
  bool mappingOnly = false;  // only reachable thru a livery mapping
  std::vector<SoundFile> files;
  std::vector<std::string> warnings;
  int maxVariant = 0;

  bool isDefault() const;
  int countOf(AnnType t) const;
  // custom unplayed type names once each
  std::vector<std::string> customTypes() const;
};

struct PackLibrary {
  std::vector<Pack> packs;
  std::vector<std::string> warnings;
  std::vector<std::string> roots;

  const Pack* find(std::string_view name) const;
  const Pack* defaultPack() const { return find("Default"); }
  size_t soundCount() const;
};

// 2-4 letters/digits or default
bool isPackIdName(std::string_view folderName);

// missing root is just a warning cancel can be null
PackLibrary scanPacks(const std::vector<std::string>& roots,
                      const std::atomic<bool>* cancel = nullptr);

}  // namespace fxa
