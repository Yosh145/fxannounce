// xplm entry points everything forwards to app inside guarded so nothing throws
// into xp
#include <XPLMDefs.h>

#include <cstring>

#include "app.h"
#include "core/guard.h"

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc) {
  std::strncpy(outName, "FXAnnounce " FXA_VERSION, 255);
  std::strncpy(outSig, "yosh.fxannounce", 255);
  std::strncpy(outDesc,
               "Fenix-style Airline Pack cabin announcements for the ToLiss "
               "Airbus family",
               255);
  return fxa::guarded("XPluginStart", 0, [] {
    fxa::App::get().start();
    return 1;
  });
}

PLUGIN_API void XPluginStop(void) {
  fxa::guarded("XPluginStop", [] { fxa::App::get().stop(); });
}

PLUGIN_API int XPluginEnable(void) {
  return fxa::guarded("XPluginEnable", 0, [] {
    fxa::App::get().enable();
    return 1;
  });
}

PLUGIN_API void XPluginDisable(void) {
  fxa::guarded("XPluginDisable", [] { fxa::App::get().disable(); });
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int msg, void* param) {
  fxa::guarded("XPluginReceiveMessage",
               [&] { fxa::App::get().onMessage(msg, param); });
}
