// what the panel shows its actions filled by the plugin each frame plain data
// std function so the headless test can drive it
#pragma once

#include <deque>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "core/config.h"
#include "core/log.h"
#include "core/pack_library.h"

namespace fxa::ui {

struct UiModel {
  std::string version;
  bool active = false;  // supported toliss loaded purser running
  bool safeMode = false;
  std::string lastFault;
  bool muted = false;
  bool overwater = false;  // overwater toggle for this flight

  // dormant loaded aircraft name
  std::string loadedAircraft;

  // status cabin card
  std::string stage;
  bool calling = false;  // pull cab on the acp
  int callAttempt = 0;
  std::string playing, playingFenix;
  std::string boardingNote;  // shortened boarding xp clock hot start
  float boardingFrac = -1;   // -1 when not boarding
  double boardingLeftSec = -1;
  int boardingPax = 0;

  // flight card
  struct Flight {
    int state = 0;  // 0 none 1 in use 2 ignored
    std::string callsign, origin, destination;
    std::string etd, boarding;
    std::string line;
    std::string airline;   // empty if the table doesnt know it
    float routeFrac = -1;  // great circle -1 unknown
    double toGoNm = -1;
    std::string reason;
  } flight;

  // aircraft card
  std::string aircraft;
  std::string livery;
  std::string pack;  // or none scanning

  // timeline
  struct Pending {
    std::string name, fenix;  // fenix empty for milestones
    double inSec = -1;        // -1 waits for condition
    std::string condition;
    std::string note;
  };
  std::vector<Pending> pending;
  struct EventRow {
    double zuluSec = -1, localSec = -1;
    std::string name, fenix, outcome, detail;
    int tone = 0;  // 0 played 1 problem 2 neutral
  };
  std::vector<EventRow> events;  // newest first

  // packs now playing
  struct Now {
    std::string file;         // empty when nothing plays
    double pos = 0, dur = 0;  // secs
    bool decoding = false, paused = false, interphone = false;
  } now;
  std::string music;
  std::string customTags;
  const PackLibrary* library = nullptr;  // null till the first scan is done
  bool scanning = false;
  std::string packsDir;
  // explainer only with show log
  std::function<std::vector<std::string>(int annType, int context)> explain;

  // log show log only
  std::deque<LogLine> log;
  std::vector<std::pair<std::string, std::string>> liveInputs;

  std::string recorderPath;

  Settings* settings = nullptr;  // edited in place by settings titlebar

  // actions any can be empty
  std::function<void()> onSkip, onAdvance, onToggleMute, onToggleOverwater,
      onRescan, onReenable;
  std::function<void()> onSettingsChanged;  // apply save
};

// panel only state kept between frames
struct UiState {
  int tab = 0;  // panel.h ids
  bool tabRestored = false;
  int settingsPage = 0;
  char logFilter[64] = {};
  char packFilter[64] = {};
  int explainType = 1;
  int explainContext = 0;
  std::vector<std::string> explainLines;
  float cardRowH[4] = {};  // 2 aircraft card 3 band table overhead
};

}  // namespace fxa::ui
