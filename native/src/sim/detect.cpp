#include "sim/detect.h"

#include "core/strings.h"
#include "sim/dataref.h"

#include <XPLMPlanes.h>

namespace fxa {

namespace {

std::string lastPathPart(std::string p)
{
    while (!p.empty() && (p.back() == '/' || p.back() == '\\')) p.pop_back();
    const size_t k = p.find_last_of("/\\");
    return k == std::string::npos ? p : p.substr(k + 1);
}

} // namespace

AircraftInfo detectAircraft()
{
    AircraftInfo info;
    char file[512] = {}, path[2048] = {};
    XPLMGetNthAircraftModel(0, file, path);
    info.acfFile = cleanCString(file, sizeof(file));
    info.acfPath = cleanCString(path, sizeof(path));
    const size_t cut = info.acfPath.find_last_of("/\\");
    if (cut != std::string::npos) info.acfDir = info.acfPath.substr(0, cut + 1);

    DataRef icao("sim/aircraft/view/acf_ICAO");
    DataRef author("sim/aircraft/view/acf_author");
    DataRef livery("sim/aircraft/view/acf_livery_path");   // slow dataref: read only here
    icao.resolve();
    author.resolve();
    livery.resolve();
    info.acfIcao = icao.getString(40);
    info.author = author.getString(500);
    info.liveryPath = livery.getString(1024);
    if (!info.liveryPath.empty()) info.liveryName = lastPathPart(info.liveryPath);

    DataRef phase("AirbusFBW/APPhase");
    DataRef v1("toliss_airbus/performance/V1");   // the marker X-Airbus uses
    const bool hasToliss = phase.resolve() || v1.resolve();
    const bool looksToliss = icontains(info.author, "kiwi") || icontains(info.acfPath, "toliss");
    info.toliss = hasToliss && looksToliss;
    info.family = familyFrom(info.acfIcao, info.acfFile);
    if (info.toliss && info.family == Family::Unknown) info.toliss = false;   // not an Airbus we model
    // Phase 2 runs on the A320 family only; the A330/A340 come later (VP_SPEC scope).
    info.supported = info.toliss && (info.family == Family::A319 || info.family == Family::A320 || info.family == Family::A321);
    return info;
}

} // namespace fxa
