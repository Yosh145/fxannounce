#include "core/pack_library.h"

#include "core/strings.h"

#include <algorithm>
#include <cctype>
#include <filesystem>

namespace fxa {

namespace fs = std::filesystem;

namespace {

constexpr int kMaxDepth = 6;          // folders inside a pack
constexpr size_t kMaxFilesPerPack = 5000;
constexpr size_t kMaxPacks = 2000;

std::string u8(const fs::path& p)
{
#if defined(__cpp_char8_t)
    const auto s = p.u8string();
    return std::string(s.begin(), s.end());
#else
    return p.u8string();
#endif
}

bool isDir(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

std::vector<fs::path> listDirs(const fs::path& dir)
{
    std::vector<fs::path> out;
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        if (isDir(it->path())) out.push_back(it->path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool hasAudioDirectly(const fs::path& dir)
{
    std::error_code ec;
    for (fs::directory_iterator it(dir, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        std::error_code ec2;
        if (it->is_regular_file(ec2) && iequals(u8(it->path().extension()), ".ogg")) return true;
    }
    return false;
}

void scanPackFiles(Pack& pack, const std::atomic<bool>* cancel)
{
    const fs::path root = fs::u8path(pack.root);
    std::error_code ec;
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
    for (; !ec && it != end; it.increment(ec)) {
        if (cancel && cancel->load()) return;
        if (it.depth() > kMaxDepth) {
            it.disable_recursion_pending();
            continue;
        }
        std::error_code ec2;
        if (it->is_symlink(ec2) && it->is_directory(ec2)) {   // never follow links inside a pack
            it.disable_recursion_pending();
            continue;
        }
        if (!it->is_regular_file(ec2)) continue;
        const fs::path& p = it->path();
        const std::string rel = u8(fs::relative(p, root, ec2));
        std::string relSlash = rel;
        std::replace(relSlash.begin(), relSlash.end(), '\\', '/');
        const std::string ext = toLower(u8(p.extension()));
        if (ext != ".ogg") {
            // Images (EFB wallpaper, FAP screensaver) and text files are expected in packs; anything else is reported.
            static const char* quiet[] = { ".png", ".jpg", ".jpeg", ".txt", ".md", ".pdf", ".ini", ".cfg", ".json", "" };
            if (std::none_of(std::begin(quiet), std::end(quiet), [&](const char* q) { return ext == q; }))
                pack.warnings.push_back(relSlash + ": not an .ogg file, skipped");
            continue;
        }
        if (pack.files.size() >= kMaxFilesPerPack) {
            pack.warnings.push_back("more than 5000 sounds: the rest are ignored");
            return;
        }
        auto parsed = parseSoundFileName(u8(p.filename()));
        if (!parsed) {
            pack.warnings.push_back(relSlash + ": no announcement type in the name, skipped");
            continue;
        }
        SoundFile f = std::move(*parsed);
        f.path = u8(p);
        f.relPath = relSlash;
        for (const std::string& w : f.warnings) pack.warnings.push_back(relSlash + ": " + w);
        if (!f.type) pack.warnings.push_back(relSlash + ": '" + f.typeName + "' is not an announcement type (FAP-only, not played)");
        if (f.type == AnnType::BoardingMusic && relSlash.find('/') != std::string::npos) {
            pack.warnings.push_back(relSlash + ": boarding music must be in the pack root (Fenix rule), not used");
            continue;
        }
        for (int v : f.variants) pack.maxVariant = std::max(pack.maxVariant, v);
        pack.files.push_back(std::move(f));
    }
    std::sort(pack.files.begin(), pack.files.end(), [](const SoundFile& a, const SoundFile& b) { return a.relPath < b.relPath; });
}

} // namespace

bool Pack::isDefault() const { return iequals(name, "Default"); }

int Pack::countOf(AnnType t) const
{
    return static_cast<int>(std::count_if(files.begin(), files.end(), [&](const SoundFile& f) { return f.type == t; }));
}

const Pack* PackLibrary::find(std::string_view name) const
{
    for (const Pack& p : packs)
        if (iequals(p.name, name)) return &p;
    return nullptr;
}

size_t PackLibrary::soundCount() const
{
    size_t n = 0;
    for (const Pack& p : packs) n += p.files.size();
    return n;
}

bool isPackIdName(std::string_view n)
{
    if (iequals(n, "Default")) return true;
    if (n.size() < 2 || n.size() > 4) return false;
    return std::all_of(n.begin(), n.end(), [](char c) { return std::isalnum(static_cast<unsigned char>(c)); });
}

PackLibrary scanPacks(const std::vector<std::string>& roots, const std::atomic<bool>* cancel)
{
    PackLibrary lib;
    lib.roots = roots;
    std::vector<Pack> found;
    for (const std::string& r : roots) {
        const fs::path root = fs::u8path(r);
        if (!isDir(root)) {
            lib.warnings.push_back("pack folder not found: " + r);
            continue;
        }
        for (const fs::path& top : listDirs(root)) {
            if (cancel && cancel->load()) return lib;
            const std::string topName = u8(top.filename());
            if (topName.empty() || topName[0] == '.') continue;
            auto add = [&](const fs::path& dir, const std::string& wrapper, bool mappingOnly) {
                Pack p;
                p.name = u8(dir.filename());
                p.root = u8(dir);
                p.wrapper = wrapper;
                p.mappingOnly = mappingOnly;
                found.push_back(std::move(p));
            };
            if (isPackIdName(topName)) {
                add(top, "", false);
                continue;
            }
            std::vector<fs::path> inner;
            for (const fs::path& sub : listDirs(top))
                if (isPackIdName(u8(sub.filename()))) inner.push_back(sub);
            if (!inner.empty()) {
                for (const fs::path& sub : inner) add(sub, topName, false);
            } else if (hasAudioDirectly(top) || !listDirs(top).empty()) {
                add(top, "", true);
            }
        }
    }
    for (Pack& p : found) {
        if (cancel && cancel->load()) return lib;
        if (lib.packs.size() >= kMaxPacks) {
            lib.warnings.push_back("more than 2000 packs: the rest are ignored");
            break;
        }
        if (const Pack* dup = lib.find(p.name)) {
            lib.warnings.push_back("duplicate pack '" + p.name + "' at " + p.root + " ignored (using " + dup->root + ")");
            continue;
        }
        scanPackFiles(p, cancel);
        if (p.files.empty()) {
            lib.warnings.push_back("pack '" + p.name + "' has no .ogg sounds: ignored");
            continue;
        }
        lib.packs.push_back(std::move(p));
    }
    std::sort(lib.packs.begin(), lib.packs.end(), [](const Pack& a, const Pack& b) { return toLower(a.name) < toLower(b.name); });
    return lib;
}

} // namespace fxa
