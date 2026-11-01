#include "core/sound_file.h"

#include "core/aircraft.h"
#include "core/strings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace fxa {

bool SoundFile::hasVariant(int v) const
{
    if (variants.empty()) return v == 0;
    return std::find(variants.begin(), variants.end(), v) != variants.end();
}

namespace {

bool allDigits(std::string_view s)
{
    if (s.empty() || s.size() > 6) return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

void classify(SoundFile& f, std::string_view tag)
{
    f.rawTags.emplace_back(tag);
    if (allDigits(tag)) {
        f.variants.push_back(std::atoi(std::string(tag).c_str()));
        return;
    }
    if (iequals(tag, "Refueling") || iequals(tag, "Refuelling")) {
        f.refuel = true;
        return;
    }
    if (auto t = timeOfDayFromTag(tag)) {
        f.times.push_back(*t);
        return;
    }
    if (auto p = phaseFromTag(tag)) {
        f.phases.push_back(*p);
        return;
    }
    if (auto m = modeFromTag(tag)) {
        f.mode = *m;
        return;
    }
    if (iequals(tag, "Overwater")) {
        f.overwater = true;
        return;
    }
    if (isAircraftTypeTag(tag)) {
        f.aircraft.push_back(toUpper(tag));
        return;
    }
    f.custom.emplace_back(tag);
}

std::string_view stripExtension(std::string_view name)
{
    const size_t dot = name.find_last_of('.');
    if (dot == std::string_view::npos || dot == 0) return name;
    const std::string_view ext = name.substr(dot + 1);
    // Only strip a plausible extension, so "Safety Demo v1.2~x" keeps its dots.
    if (ext.size() >= 2 && ext.size() <= 4 && std::all_of(ext.begin(), ext.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); }))
        return name.substr(0, dot);
    return name;
}

} // namespace

std::optional<SoundFile> parseSoundFileName(std::string_view fileName)
{
    const std::string_view base = stripExtension(fileName);
    SoundFile f;
    // Everything after the first '~' is the display name; tags must come before it (Fenix rule).
    const size_t tilde = base.find('~');
    const std::string_view head = base.substr(0, tilde);
    const std::string_view tail = tilde == std::string_view::npos ? std::string_view() : base.substr(tilde + 1);

    const size_t firstBracket = head.find('[');
    f.typeName = std::string(trim(head.substr(0, firstBracket)));
    if (f.typeName.empty()) return std::nullopt;

    size_t pos = firstBracket;
    while (pos != std::string_view::npos && pos < head.size()) {
        const size_t close = head.find(']', pos);
        if (close == std::string_view::npos) {
            f.warnings.push_back("unclosed '[' in name");
            break;
        }
        const std::string_view tag = trim(head.substr(pos + 1, close - pos - 1));
        if (tag.empty())
            f.warnings.push_back("empty [] tag");
        else
            classify(f, tag);
        const size_t next = head.find('[', close + 1);
        const std::string_view between = trim(head.substr(close + 1, next == std::string_view::npos ? std::string_view::npos : next - close - 1));
        if (!between.empty()) f.warnings.push_back("text '" + std::string(between) + "' between tags ignored");
        pos = next;
    }

    if (auto tn = resolveTypeName(f.typeName)) {
        f.type = tn->type;
        f.context = tn->context;
        f.legacy = tn->legacy;
    }
    f.displayName = std::string(trim(tail));
    if (f.displayName.empty()) f.displayName = std::string(base);
    return f;
}

std::string describeTags(const SoundFile& f)
{
    std::string out = f.typeName;
    for (const std::string& t : f.rawTags) out += "[" + t + "]";
    return out;
}

} // namespace fxa
