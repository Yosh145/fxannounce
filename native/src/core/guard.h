// Fault guard for every function X-Plane calls into (ARCHITECTURE.md § 4): exceptions never cross into the
// sim. After kMaxFaults faults the plugin is put into safe mode and the owner of the safe-mode hook shuts
// the plugin's activity down.
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
    // Called once when the plugin enters safe mode (the plugin stops audio and unregisters callbacks there).
    void setSafeModeHook(std::function<void()> hook) { hook_ = std::move(hook); }
    // Leaves safe mode and clears the fault count (UI "Re-enable").
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

// Runs fn; on any exception logs it, counts a fault and returns fallback.
template <typename R, typename F>
R guarded(const char* where, R fallback, F&& fn) noexcept
{
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
void guarded(const char* where, F&& fn) noexcept
{
    try {
        fn();
    } catch (const std::exception& e) {
        FaultGuard::get().recordFault(where, e.what());
    } catch (...) {
        FaultGuard::get().recordFault(where, "unknown exception");
    }
}

} // namespace fxa
