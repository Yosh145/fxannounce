#include "sim/probe_census.h"

#include <XPLMDataAccess.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace fxa {

namespace {

bool wanted(const char* name)
{
    static const char* prefixes[] = { "AirbusFBW/", "toliss_airbus/", "ckpt/", "sbh/" };
    for (const char* p : prefixes)
        if (std::strncmp(name, p, std::strlen(p)) == 0) return true;
    return false;
}

// The value as short text: scalars, the first 32 array entries, up to 64 bytes of a byte array.
std::string valueText(XPLMDataRef r, XPLMDataTypeID t)
{
    char buf[64];
    std::string out;
    if (t & xplmType_Int) {
        std::snprintf(buf, sizeof(buf), "%d", XPLMGetDatai(r));
        return buf;
    }
    if (t & xplmType_Float) {
        std::snprintf(buf, sizeof(buf), "%.3g", static_cast<double>(XPLMGetDataf(r)));
        return buf;
    }
    if (t & xplmType_Double) {
        std::snprintf(buf, sizeof(buf), "%.4g", XPLMGetDatad(r));
        return buf;
    }
    if (t & xplmType_FloatArray) {
        float v[32] = {};
        const int n = std::clamp(XPLMGetDatavf(r, v, 0, 32), 0, 32);
        for (int k = 0; k < n; ++k) {
            std::snprintf(buf, sizeof(buf), "%s%.3g", k ? " " : "", static_cast<double>(v[k]));
            out += buf;
        }
        return out;
    }
    if (t & xplmType_IntArray) {
        int v[32] = {};
        const int n = std::clamp(XPLMGetDatavi(r, v, 0, 32), 0, 32);
        for (int k = 0; k < n; ++k) {
            std::snprintf(buf, sizeof(buf), "%s%d", k ? " " : "", v[k]);
            out += buf;
        }
        return out;
    }
    if (t & xplmType_Data) {
        char bytes[65] = {};
        const int n = std::clamp(XPLMGetDatab(r, bytes, 0, 64), 0, 64);
        bytes[n] = '\0';
        return std::string(bytes, std::strlen(bytes));
    }
    return out;
}

} // namespace

std::map<std::string, std::string> ProbeCensus::read()
{
    std::map<std::string, std::string> out;
    const int count = XPLMCountDataRefs();
    if (count <= 0) return out;
    std::vector<XPLMDataRef> refs(static_cast<size_t>(count));
    XPLMGetDataRefsByIndex(0, count, refs.data());
    for (XPLMDataRef r : refs) {
        if (!r) continue;
        XPLMDataRefInfo_t info{};
        info.structSize = sizeof(info);
        XPLMGetDataRefInfo(r, &info);
        if (!info.name || !wanted(info.name) || !XPLMIsDataRefGood(r)) continue;
        out[info.name] = valueText(r, info.type);
    }
    return out;
}

size_t ProbeCensus::snapshot()
{
    base_ = read();
    return base_.size();
}

std::vector<std::string> ProbeCensus::changed() const
{
    std::vector<std::string> out;
    if (base_.empty()) return out;
    for (const auto& [name, value] : read()) {
        const auto it = base_.find(name);
        if (it == base_.end() || it->second != value) out.push_back(name);
    }
    return out;
}

} // namespace fxa
