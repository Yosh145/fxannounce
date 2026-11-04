// trace recorder 1hz state rows purser events to csv plus the reader for replay
// tests
#pragma once

#include <cstdio>
#include <string>
#include <vector>

#include "core/sim_state.h"

namespace fxa {

class Recorder {
 public:
  ~Recorder() { close(); }
  bool open(const std::string& path);
  void close();
  bool isOpen() const { return file_ != nullptr; }
  const std::string& path() const { return path_; }

  void state(const SimState& s, double interval = 1.0);
  void event(double t, const std::string& what);

 private:
  FILE* file_ = nullptr;
  std::string path_;
  double lastState_ = -1e9;
};

struct TraceRow {
  bool isEvent = false;
  SimState state;
  std::string event;
};

// hand written fixtures use this too
const char* traceHeader();
// bad lines skipped
std::vector<TraceRow> readTrace(const std::string& csvText);
std::string traceLine(const SimState& s);

}  // namespace fxa
