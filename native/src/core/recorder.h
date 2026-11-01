// Flight trace recorder: SimState rows (1 Hz) and purser events to CSV, and the reader the replay tests use.
#pragma once

#include "core/sim_state.h"

#include <cstdio>
#include <string>
#include <vector>

namespace fxa {

class Recorder {
public:
    ~Recorder() { close(); }
    bool open(const std::string& path);   // creates the folder; returns false on failure
    void close();
    bool isOpen() const { return file_ != nullptr; }
    const std::string& path() const { return path_; }

    // Writes a state row if at least `interval` seconds of purser time passed since the last one.
    void state(const SimState& s, double interval = 1.0);
    // Writes an event row immediately ("ArmDoors played DLH/ArmDoors.ogg").
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

// CSV header line written by the recorder (also used by hand-written trace fixtures).
const char* traceHeader();
// Parses a trace written by Recorder (or by hand with the same header). Bad lines are skipped.
std::vector<TraceRow> readTrace(const std::string& csvText);
std::string traceLine(const SimState& s);

} // namespace fxa
