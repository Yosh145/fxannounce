#include "core/guard.h"

#include "core/log.h"

namespace fxa {

FaultGuard& FaultGuard::get() {
  static FaultGuard instance;
  return instance;
}

void FaultGuard::reset() {
  faults_ = 0;
  safeMode_ = false;
  lastFault_.clear();
  logSummary("re-enabled after safe mode");
}

void FaultGuard::recordFault(const char* where, const char* what) {
  ++faults_;
  lastFault_ = std::string(where) + ": " + what;
  logError("fault %d/%d in %s: %s", faults_, kMaxFaults, where, what);
  if (faults_ >= kMaxFaults && !safeMode_) {
    safeMode_ = true;
    logError(
        "too many faults: entering safe mode (audio stopped, callbacks "
        "removed)");
    if (hook_ && !inHook_) {
      inHook_ = true;
      try {
        hook_();
      } catch (...) {
        logError("safe-mode hook threw; continuing in safe mode");
      }
      inHook_ = false;
    }
  }
}

}  // namespace fxa
