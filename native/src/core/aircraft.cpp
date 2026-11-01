#include "core/aircraft.h"

#include "core/strings.h"

#include <cctype>

namespace fxa {

const char* familyName(Family f)
{
    switch (f) {
    case Family::A319: return "A319";
    case Family::A320: return "A320";
    case Family::A321: return "A321";
    case Family::A330: return "A330";
    case Family::A340: return "A340";
    default: return "unknown";
    }
}

Family familyFrom(std::string_view icao, std::string_view acfFile)
{
    const std::string i = toUpper(trim(icao));
    if (i == "A319" || i == "A19N") return Family::A319;
    if (i == "A320" || i == "A20N") return Family::A320;
    if (i == "A321" || i == "A21N") return Family::A321;
    if (i.rfind("A33", 0) == 0) return Family::A330;
    if (i.rfind("A34", 0) == 0) return Family::A340;
    const std::string f = toLower(acfFile);
    if (f.rfind("a319", 0) == 0) return Family::A319;
    if (f.rfind("a320", 0) == 0) return Family::A320;
    if (f.rfind("a321", 0) == 0) return Family::A321;
    if (f.rfind("a330", 0) == 0 || f.rfind("a339", 0) == 0) return Family::A330;
    if (f.rfind("a340", 0) == 0 || f.rfind("a346", 0) == 0) return Family::A340;
    return Family::Unknown;
}

int engineIsNeo(std::string_view code)
{
    const std::string c = toUpper(trim(code));
    if (c.empty()) return -1;
    if (c.rfind("PW", 0) == 0 || c.rfind("LEA", 0) == 0) return 1;
    if (c.rfind("CFM", 0) == 0 || c.rfind("IAE", 0) == 0) return 0;
    return -1;
}

std::string liveryStringCode(std::string_view name)
{
    name = trim(name);
    if (name.size() < 3 || name[0] != '[') return {};
    const size_t close = name.find(']');
    if (close == std::string_view::npos) return {};
    return toUpper(name.substr(1, close - 1));
}

AircraftType makeAircraftType(Family f, bool neo)
{
    AircraftType t;
    t.family = f;
    switch (f) {
    case Family::A319:
        t.tags = { "A319" };
        t.typeFactor = 0.9f;
        t.label = "A319";
        break;
    case Family::A320:
        t.neo = neo;
        t.tags = neo ? std::vector<std::string>{ "A320", "A20N" } : std::vector<std::string>{ "A320" };
        t.typeFactor = 1.0f;
        t.label = neo ? "A320neo" : "A320";
        break;
    case Family::A321:
        t.neo = neo;
        t.tags = neo ? std::vector<std::string>{ "A321", "A21N" } : std::vector<std::string>{ "A321" };
        t.typeFactor = 1.25f;
        t.label = neo ? "A321neo" : "A321";
        break;
    case Family::A330:
        t.neo = true;
        t.tags = { "A330", "A339" };
        t.typeFactor = 1.6f;
        t.label = "A330-900";
        break;
    case Family::A340:
        t.tags = { "A340", "A346" };
        t.typeFactor = 1.8f;
        t.label = "A340-600";
        break;
    default:
        t.label = "unknown";
        break;
    }
    t.specific = t.tags.empty() ? std::string() : t.tags.back();
    return t;
}

bool isAircraftTypeTag(std::string_view tag)
{
    const std::string t = toUpper(trim(tag));
    const size_t n = t.size();
    if (n < 3 || n > 4) return false;
    auto digit = [&](size_t k) { return k < n && std::isdigit(static_cast<unsigned char>(t[k])); };
    auto alnum = [&](size_t k) { return k < n && std::isalnum(static_cast<unsigned char>(t[k])); };
    // Manufacturer letter + 2-3 digits + optional letter/digit: A320 A21N A20N A339 B738 B38M B77W B788 E190 E75L
    if ((t[0] == 'A' || t[0] == 'B' || t[0] == 'E') && digit(1) && digit(2) && (n == 3 || alnum(3))) return true;
    // A few families whose designators start with letters.
    static const char* prefixes[] = { "CRJ", "DH8", "AT4", "AT7", "BCS", "MD8", "MD9", "MD1", "C919", "SU9", "A3ST" };
    for (const char* p : prefixes)
        if (t.rfind(p, 0) == 0) return true;
    return false;
}

} // namespace fxa
