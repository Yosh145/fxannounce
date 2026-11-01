#include "core/pack_resolver.h"

#include "core/aircraft.h"
#include "core/strings.h"

#include <algorithm>
#include <cctype>

namespace fxa {

namespace {

void readKeys(const Ini::Section& s, LiveryConfig& c)
{
    for (const auto& [k, v] : s) {
        if (iequals(k, "airline") || iequals(k, "icao_airline")) c.airline = std::string(trim(v));
        else if (iequals(k, "custom_tags") || iequals(k, "fnx_airline_pack_custom_tags")) c.customTags = splitList(v);
        else if (iequals(k, "overwater")) {
            if (iequals(trim(v), "auto") || trim(v).empty()) c.overwater.reset();
            else c.overwater = parseBool(v, false);
        }
    }
}

bool isLetter(char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }
bool isAlnum(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0; }

// Livery name tokens that can be an ICAO airline code: three letters, outside the ToLiss [CODE] prefix, not the
// part of a registration after its dash ("B-LCA"), and not an engine name ToLiss liveries often carry ("CFM").
std::vector<std::string> icaoTokens(const std::string& liveryName)
{
    std::string name = liveryName;
    if (!liveryStringCode(name).empty()) name = name.substr(name.find(']') + 1);
    static const char* engines[] = { "CFM", "IAE", "LEA", "PWG" };
    std::vector<std::string> out;
    std::string cur;
    bool afterDash = false;
    for (size_t k = 0; k <= name.size(); ++k) {
        const char c = k < name.size() ? name[k] : ' ';
        if (isAlnum(c)) {
            if (cur.empty()) afterDash = k > 0 && name[k - 1] == '-';
            cur += c;
            continue;
        }
        const std::string up = toUpper(cur);
        if (cur.size() == 3 && !afterDash && std::all_of(cur.begin(), cur.end(), isLetter) &&
            std::none_of(std::begin(engines), std::end(engines), [&](const char* e) { return up == e; }))
            out.push_back(up);
        cur.clear();
    }
    return out;
}

} // namespace

LiveryConfig liveryConfigFromSidecar(const Ini& sidecar)
{
    LiveryConfig c;
    for (const auto& [name, sect] : sidecar.sections()) readKeys(sect, c);
    if (!c.airline.empty() || !c.customTags.empty() || c.overwater) c.source = "airlinepack.cfg";
    return c;
}

LiveryConfig liveryConfigFromCentral(const Ini& central, const std::string& liveryName)
{
    LiveryConfig c;
    if (liveryName.empty()) return c;
    for (const auto& [glob, sect] : central.sections()) {
        if (glob.empty() || !globMatch(glob, liveryName)) continue;
        readKeys(sect, c);
        c.source = "liveries.ini [" + glob + "]";
        break;
    }
    return c;
}

LiveryConfig mergeLiveryConfig(const LiveryConfig& side, const LiveryConfig& central)
{
    LiveryConfig c = central;
    if (!side.airline.empty()) c.airline = side.airline;
    if (!side.customTags.empty()) c.customTags = side.customTags;
    if (side.overwater) c.overwater = side.overwater;
    if (!side.airline.empty()) c.source = side.source;
    else if (c.source.empty()) c.source = side.source;
    return c;
}

std::vector<std::string> airlineCodesFromFlightNumber(const AirlineTable& airlines, const std::string& fn)
{
    std::string f = toUpper(trim(fn));
    std::vector<std::string> out;
    if (f.size() < 3) return out;
    if (isLetter(f[0]) && isLetter(f[1]) && isLetter(f[2])) out.push_back(f.substr(0, 3));
    // IATA: two alphanumerics (not both digits) then the flight digits.
    if (isAlnum(f[0]) && isAlnum(f[1]) && !(std::isdigit(static_cast<unsigned char>(f[0])) && std::isdigit(static_cast<unsigned char>(f[1])))) {
        const size_t rest = (f.size() > 2 && isLetter(f[2])) ? std::string::npos : 2;
        if (rest != std::string::npos)
            for (const std::string& icao : airlines.icaoForIata(f.substr(0, 2))) out.push_back(icao);
    }
    return out;
}

Resolution resolvePack(const PackLibrary& lib, const AirlineTable& airlines, const ResolveInput& in)
{
    Resolution r;
    // 1-2. Livery mapping (sidecar or central).
    if (!in.livery.airline.empty()) {
        if (const Pack* p = lib.find(in.livery.airline)) {
            r.pack = p;
            r.reason = "livery mapping (" + in.livery.source + ")";
            return r;
        }
    }
    // 3. SimBrief OFP callsign (checked by the caller: its origin is this airport).
    for (const std::string& code : airlineCodesFromFlightNumber(airlines, in.ofpCallsign)) {
        if (const Pack* p = lib.find(code); p && !p->mappingOnly && !p->isDefault()) {
            r.pack = p;
            r.reason = "SimBrief OFP " + toUpper(trim(in.ofpCallsign));
            return r;
        }
    }
    // 4. Flight number.
    for (const std::string& code : airlineCodesFromFlightNumber(airlines, in.flightNumber)) {
        if (const Pack* p = lib.find(code); p && !p->mappingOnly && !p->isDefault()) {
            r.pack = p;
            r.reason = "flight number " + toUpper(trim(in.flightNumber));
            return r;
        }
    }
    // 5a. An installed pack's code in the livery folder name.
    for (const std::string& tok : icaoTokens(in.liveryName)) {
        if (const Pack* p = lib.find(tok); p && !p->mappingOnly && !p->isDefault()) {
            r.pack = p;
            r.reason = "livery name contains " + tok;
            return r;
        }
    }
    // 5b. The airline's name in the livery folder name.
    for (const std::string& code : airlines.icaoForLiveryName(in.liveryName)) {
        if (const Pack* p = lib.find(code); p && !p->mappingOnly && !p->isDefault()) {
            r.pack = p;
            r.reason = "livery name looks like " + code;
            return r;
        }
    }
    // 6. Default.
    if (const Pack* p = lib.defaultPack()) {
        r.pack = p;
        r.reason = in.livery.airline.empty() ? "no airline match: Default pack"
                                             : "pack '" + in.livery.airline + "' not installed: Default pack";
        return r;
    }
    r.reason = "no airline match and no Default pack installed";
    return r;
}

} // namespace fxa
