// wraps every xplm callback exceptions never reach the sim kmaxfaults faults
// safe mode architecture 4
#pragma once

#include <exception>
#include <functional>
#include <string>

namespace fxa {

class FaultGuard {
 public:
  static constexpr int kMaxFaults = 3;
  static FaultGuard& get();

  int faults() const { return faults_; }
  bool safeMode() const { return safeMode_; }
  const std::string& lastFault() const { return lastFault_; }
  // safe mode hook plugin stops audio callbacks there
  void setSafeModeHook(std::function<void()> hook) { hook_ = std::move(hook); }
  // re-enable
  void reset();

  void recordFault(const char* where, const char* what);

 private:
  FaultGuard() = default;
  int faults_ = 0;
  bool safeMode_ = false;
  bool inHook_ = false;
  std::string lastFault_;
  std::function<void()> hook_;
};

template <typename R, typename F>
R guarded(const char* where, R fallback, F&& fn) noexcept {
  try {
    return fn();
  } catch (const std::exception& e) {
    FaultGuard::get().recordFault(where, e.what());
  } catch (...) {
    FaultGuard::get().recordFault(where, "unknown exception");
  }
  return fallback;
}

template <typename F>
void guarded(const char* where, F&& fn) noexcept {
  try {
    fn();
  } catch (const std::exception& e) {
    FaultGuard::get().recordFault(where, e.what());
  } catch (...) {
    FaultGuard::get().recordFault(where, "unknown exception");
  }
}

}  // namespace fxa
