#include "audio/player.h"

#include "audio/chime.h"
#include "core/guard.h"
#include "core/log.h"

#include <XPLMSound.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace fxa {

namespace {

// Buffers handed to FMOD stay alive until X-Plane's completion callback for that channel (it may come inside
// XPLMStopAudio or later). Keyed by (slot, generation).
std::map<uint64_t, std::shared_ptr<PcmBuffer>>& parked()
{
    static std::map<uint64_t, std::shared_ptr<PcmBuffer>> p;
    return p;
}

Player* gPlayer = nullptr;   // the one live player, for the C callback

uint64_t key(int slot, unsigned gen) { return (static_cast<uint64_t>(gen) << 3) | static_cast<uint64_t>(slot); }

std::string mmss(double s)
{
    if (s < 0) s = 0;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d", static_cast<int>(s) / 60, static_cast<int>(s) % 60);
    return buf;
}

} // namespace

void channelComplete(void* refcon, FMOD_RESULT status);

void releaseParkedAudio() { parked().clear(); }

Player::Player(DecodeCache& cache) : cache_(cache)
{
    for (int k = 0; k < kSlots; ++k) voices_[k].slot = k;
    voices_[kMusic].kind = SoundKind::Music;
    voices_[kChime].kind = SoundKind::Chime;
    voices_[kPreview].kind = SoundKind::Preview;
    chimePcm_ = makeCabinChime();
    gPlayer = this;
}

Player::~Player()
{
    stopAll();
    if (gPlayer == this) gPlayer = nullptr;
}

// XPLMPCMComplete_f: called on the main thread, once per channel, also from inside XPLMStopAudio.
void channelComplete(void* refcon, FMOD_RESULT)
{
    guarded("audio.complete", [&] {
        const uint64_t k = reinterpret_cast<uintptr_t>(refcon);
        parked().erase(k);
        if (gPlayer) gPlayer->onChannelDone(k);
    });
}

void Player::onChannelDone(uint64_t k)
{
    const int slot = static_cast<int>(k & 7);
    const unsigned gen = static_cast<unsigned>(k >> 3);
    if (slot < 0 || slot >= kSlots) return;
    Voice& v = voices_[slot];
    if (v.gen != gen) return;   // an older channel of this slot
    v.channel = nullptr;
    if (!v.held) v.done = true;   // finished (or killed by X-Plane), not stopped by us to resume
}

void Player::setSource(const Pack* pack, const SelectContext& base, const Settings* set, const SimState* sim, bool muted)
{
    if (pack != pack_) {
        lastByType_.clear();
        lastMusic_.clear();
        if (voices_[kMusic].active) clearVoice(voices_[kMusic]);   // the new pack's music takes over
    }
    pack_ = pack;
    base_ = base;
    set_ = set;
    sim_ = sim;
    muted_ = muted;
}

SelectContext Player::ctxFor(CallContext c) const
{
    SelectContext s = base_;
    s.context = c;
    return s;
}

int Player::busFor(const Voice& v) const
{
    if (!set_) return xplm_AudioInterior;
    switch (v.kind) {
    case SoundKind::Pa:
    case SoundKind::Music: return std::clamp(set_->paBus, 0, 9);
    case SoundKind::Interphone:
    case SoundKind::Chime: return std::clamp(set_->interphoneBus, 0, 9);
    case SoundKind::Preview: return xplm_AudioUI;
    }
    return xplm_AudioInterior;
}

float Player::gainFor(const Voice& v) const
{
    if (!set_ || !sim_) return 0.0f;
    return soundGain(v.kind, *set_, *sim_, muted_);
}

bool Player::startVoice(Voice& v, double from)
{
    if (!v.pcm || v.pcm->samples.empty()) return false;
    const size_t total = v.pcm->samples.size();
    size_t first = static_cast<size_t>(std::max(0.0, from) * v.pcm->rate);
    if (first >= total) {
        v.done = true;
        return false;
    }
    ++v.gen;
    const uint64_t k = key(v.slot, v.gen);
    parked()[k] = v.pcm;
    void* data = v.pcm->samples.data() + first;
    const uint32_t bytes = static_cast<uint32_t>((total - first) * sizeof(int16_t));
    FMOD_CHANNEL* ch = XPLMPlayPCMOnBus(data, bytes, FMOD_SOUND_FORMAT_PCM16, v.pcm->rate, 1, 0, busFor(v),
                                        channelComplete,
                                        reinterpret_cast<void*>(static_cast<uintptr_t>(k)));
    if (!ch) {
        // On failure X-Plane calls the callback with the error; the parked buffer goes then.
        logWarn("audio: X-Plane could not start %s", v.label.c_str());
        v.done = true;
        return false;
    }
    v.channel = ch;
    v.pos = static_cast<double>(first) / v.pcm->rate;
    v.held = false;
    XPLMSetAudioVolume(ch, gainFor(v));
    return true;
}

void Player::haltVoice(Voice& v)
{
    if (!v.channel) return;
    v.held = true;   // the completion callback must not mark it done
    FMOD_CHANNEL* ch = static_cast<FMOD_CHANNEL*>(v.channel);
    v.channel = nullptr;
    ++v.gen;         // any late callback of this channel is now stale
    XPLMStopAudio(ch);
}

void Player::clearVoice(Voice& v)
{
    haltVoice(v);
    v.active = v.held = v.done = false;
    v.pcm.reset();
    v.path.clear();
    v.label.clear();
    v.pos = 0;
}

void Player::stopAll()
{
    for (Voice& v : voices_) clearVoice(v);
    musicWanted_ = false;
}

void Player::prefetch(AnnType type, CallContext ctx)
{
    if (!pack_) return;
    Rng copy = rng_;
    const Selection s = selectSound(*pack_, type, ctxFor(ctx), copy, lastByType_[static_cast<int>(type)]);
    if (s.file) cache_.request(s.file->path);
}

// ---- PurserOutput -------------------------------------------------------------------

std::optional<Mode> Player::peek(AnnType type, CallContext ctx) const
{
    if (!pack_) return std::nullopt;
    Rng copy = rng_;
    const auto it = lastByType_.find(static_cast<int>(type));
    const Selection s = selectSound(*pack_, type, ctxFor(ctx), copy, it == lastByType_.end() ? std::string() : it->second);
    if (!s.file || cache_.failed(s.file->path)) return std::nullopt;
    return s.file->effectiveMode();
}

std::optional<std::string> Player::play(AnnType type, CallContext ctx, bool interphone)
{
    if (!pack_) return std::nullopt;
    const Selection s = selectSound(*pack_, type, ctxFor(ctx), rng_, lastByType_[static_cast<int>(type)]);
    if (!s.file) {
        for (const Candidate& c : s.candidates) logDebug("  %s: %s", c.file->relPath.c_str(), c.why.c_str());
        return std::nullopt;
    }
    std::string err;
    if (cache_.failed(s.file->path, &err)) {
        logWarn("audio: %s/%s cannot be decoded (%s): skipped", pack_->name.c_str(), s.file->relPath.c_str(), err.c_str());
        return std::nullopt;
    }
    Voice& v = voices_[kPa];
    clearVoice(v);
    v.kind = interphone ? SoundKind::Interphone : SoundKind::Pa;
    v.path = s.file->path;
    v.label = pack_->name + "/" + s.file->relPath;
    v.active = true;
    lastByType_[static_cast<int>(type)] = s.file->path;
    if ((v.pcm = cache_.get(v.path))) startVoice(v, 0);   // otherwise update() starts it when decoded
    if (voices_[kMusic].channel) haltVoice(voices_[kMusic]);   // music pauses under announcements
    return v.label;
}

bool Player::playing() const
{
    const Voice& v = voices_[kPa];
    return v.active && !v.done;
}

void Player::stop() { clearVoice(voices_[kPa]); }

void Player::chime()
{
    Voice& v = voices_[kChime];
    clearVoice(v);
    v.pcm = chimePcm_;
    v.label = "cabin call chime";
    v.active = true;
    startVoice(v, 0);
}

void Player::setMusic(bool on)
{
    musicWanted_ = on;
    if (!on) clearVoice(voices_[kMusic]);
}

void Player::nextMusicTrack()
{
    Voice& v = voices_[kMusic];
    clearVoice(v);
    if (!pack_) return;
    Rng& r = rng_;
    const Selection s = selectSound(*pack_, AnnType::BoardingMusic, ctxFor(CallContext::Any), r, lastMusic_);
    if (!s.file || cache_.failed(s.file->path)) return;
    v.path = s.file->path;
    v.label = pack_->name + "/" + s.file->relPath;
    v.active = true;
    lastMusic_ = s.file->path;
}

// ---- per tick ---------------------------------------------------------------------------

void Player::update(float dt)
{
    if (!set_ || !sim_) return;
    const bool hold = sim_->paused || sim_->replay;
    Voice& pa = voices_[kPa];
    const bool paBusy = pa.active && !pa.done;

    for (Voice& v : voices_) {
        if (!v.active || v.done) continue;
        if (v.channel) {
            if (!hold) v.pos += std::max(0.0f, dt);
            XPLMSetAudioVolume(static_cast<FMOD_CHANNEL*>(v.channel), gainFor(v));
            if (v.slot == kPa) lastPaGain_ = gainFor(v);
        }
    }

    // Pause / replay: stop and remember the position; resume when the sim runs again (VP_SPEC § 4.4).
    for (Voice& v : voices_) {
        if (!v.active || v.done) continue;
        if (hold) {
            if (v.channel) haltVoice(v);
            continue;
        }
        if (v.slot == kMusic) continue;   // handled below
        if (!v.channel) {
            if (!v.pcm) v.pcm = cache_.get(v.path);
            std::string err;
            if (!v.pcm && !v.path.empty() && cache_.failed(v.path, &err)) {
                logWarn("audio: %s cannot be decoded (%s)", v.label.c_str(), err.c_str());
                v.done = true;
                continue;
            }
            if (v.pcm) startVoice(v, v.pos);
        }
    }

    // Boarding music: a playlist of the pack's BoardingMusic files, paused while an announcement plays.
    Voice& m = voices_[kMusic];
    if (!musicWanted_) {
        if (m.active) clearVoice(m);
    } else if (!hold) {
        if (!m.active || m.done) nextMusicTrack();
        if (m.active && !m.done) {
            const bool under = paBusy || (voices_[kChime].active && !voices_[kChime].done);
            if (under) {
                if (m.channel) haltVoice(m);
            } else if (!m.channel) {
                if (!m.pcm) m.pcm = cache_.get(m.path);
                if (m.pcm) startVoice(m, m.pos);
                else if (cache_.failed(m.path)) m.done = true;
            }
        }
    }

    // Finished one-shots release their buffers.
    for (Voice& v : voices_)
        if (v.active && v.done && v.slot != kPa && v.slot != kMusic) clearVoice(v);
}

// ---- preview and labels -----------------------------------------------------------------

void Player::preview(const std::string& path, const std::string& label)
{
    Voice& v = voices_[kPreview];
    clearVoice(v);
    v.path = path;
    v.label = label;
    v.active = true;
    if ((v.pcm = cache_.get(path))) startVoice(v, 0);
}

void Player::stopPreview() { clearVoice(voices_[kPreview]); }

bool Player::previewing() const { return voices_[kPreview].active && !voices_[kPreview].done; }

std::string Player::previewLabel() const { return previewing() ? voices_[kPreview].label : std::string(); }

std::string Player::nowPlaying() const
{
    const Voice& v = voices_[kPa];
    if (!v.active || v.done) return {};
    if (!v.pcm) return v.label + " (decoding...)";
    return v.label + "  " + mmss(v.pos) + " / " + mmss(v.pcm->seconds()) + (v.held ? " (paused)" : "");
}

float Player::progress() const
{
    const Voice& v = voices_[kPa];
    if (!v.active || !v.pcm || v.pcm->seconds() <= 0) return 0;
    return static_cast<float>(std::clamp(v.pos / v.pcm->seconds(), 0.0, 1.0));
}

std::string Player::musicLabel() const
{
    const Voice& v = voices_[kMusic];
    if (!v.active || v.done) return {};
    return v.label + (v.channel ? "" : " (paused)");
}

} // namespace fxa
