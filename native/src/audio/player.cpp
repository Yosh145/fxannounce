#include "audio/player.h"

#include <XPLMSound.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "audio/chime.h"
#include "core/guard.h"
#include "core/log.h"

namespace fxa {

namespace {

// buffers stay alive till xps completion callback for that channel can fire
// inside xplmstopaudio keyed by slot generation
std::map<uint64_t, std::shared_ptr<PcmBuffer>>& parked() {
  static std::map<uint64_t, std::shared_ptr<PcmBuffer>> p;
  return p;
}

Player* gPlayer = nullptr;  // for the c callback

uint64_t key(int slot, unsigned gen) {
  return (static_cast<uint64_t>(gen) << 3) | static_cast<uint64_t>(slot);
}

}  // namespace

void channelComplete(void* refcon, FMOD_RESULT status);

void releaseParkedAudio() { parked().clear(); }

Player::Player(DecodeCache& cache) : cache_(cache) {
  for (int k = 0; k < kSlots; ++k) voices_[k].slot = k;
  voices_[kMusic].kind = SoundKind::Music;
  voices_[kChime].kind = SoundKind::Chime;
  chimePcm_ = makeCockpitBuzzer();
  gPlayer = this;
}

Player::~Player() {
  stopAll();
  if (gPlayer == this) gPlayer = nullptr;
}

// main thread once per channel also from inside xplmstopaudio
void channelComplete(void* refcon, FMOD_RESULT) {
  guarded("audio.complete", [&] {
    const uint64_t k = reinterpret_cast<uintptr_t>(refcon);
    parked().erase(k);
    if (gPlayer) gPlayer->onChannelDone(k);
  });
}

void Player::onChannelDone(uint64_t k) {
  static_assert(kSlots <= kTwinBit, "slot ids must fit below the twin bit");
  const int id = static_cast<int>(k & 7);
  const unsigned gen = static_cast<unsigned>(k >> 3);
  const int slot = id & (kTwinBit - 1);
  if (slot < 0 || slot >= kSlots) return;
  Voice& v = voices_[slot];
  if (id & kTwinBit) {
    // only the clear channel ends the sound never the twin
    if (v.twinGen == gen) v.twin = nullptr;
    return;
  }
  if (v.gen != gen) return;  // older channel of this slot
  v.channel = nullptr;
  if (!v.held) v.done = true;  // finished or xp killed it
}

void Player::setSource(const Pack* pack, const SelectContext& base,
                       const Settings* set, const SimState* sim, bool muted) {
  if (pack != pack_) {
    lastByType_.clear();
    lastMusic_.clear();
    if (voices_[kMusic].active)
      clearVoice(voices_[kMusic]);  // new packs music takes over
  }
  pack_ = pack;
  base_ = base;
  set_ = set;
  sim_ = sim;
  muted_ = muted;
}

SelectContext Player::ctxFor(CallContext c) const {
  SelectContext s = base_;
  s.context = c;
  return s;
}

int Player::busFor(const Voice& v) const {
  if (!set_) return xplm_AudioInterior;
  switch (v.kind) {
    case SoundKind::Pa:
    case SoundKind::Music:
      return std::clamp(set_->paBus, 0, 9);
    case SoundKind::Interphone:
    case SoundKind::Chime:
      return std::clamp(set_->interphoneBus, 0, 9);
  }
  return xplm_AudioInterior;
}

float Player::gainFor(const Voice& v) const {
  if (!set_ || !sim_) return 0.0f;
  return soundGain(v.kind, *set_, *sim_, muted_);
}

float Player::twinGainFor(const Voice& v) const {
  if (!set_ || !sim_) return 0.0f;
  return muffledGain(v.kind, *set_, *sim_, muted_);
}

bool Player::wantsTwin(const Voice& v) const {
  // cabin sounds only and only with a muffled copy interphone reuses the pa
  // slot
  return set_ && set_->muffle && set_->realisticHearing &&
         (v.kind == SoundKind::Pa || v.kind == SoundKind::Music) && v.pcm &&
         v.pcm->muffled.size() == v.pcm->samples.size();
}

bool Player::startVoice(Voice& v, double from) {
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
  const uint32_t bytes =
      static_cast<uint32_t>((total - first) * sizeof(int16_t));
  FMOD_CHANNEL* ch = XPLMPlayPCMOnBus(
      data, bytes, FMOD_SOUND_FORMAT_PCM16, v.pcm->rate, 1, 0, busFor(v),
      channelComplete, reinterpret_cast<void*>(static_cast<uintptr_t>(k)));
  if (!ch) {
    // on failure xp still calls back w the error and the parked buffer goes
    // then
    logWarn("audio: X-Plane could not start %s", v.label.c_str());
    v.done = true;
    return false;
  }
  v.channel = ch;
  v.pos = static_cast<double>(first) / v.pcm->rate;
  v.held = false;
  XPLMSetAudioVolume(ch, gainFor(v));
  // twin starts same frame same sample so they stay in step architecture 2
  haltTwin(v);
  if (wantsTwin(v)) {
    ++v.twinGen;
    const uint64_t tk = key(v.slot | kTwinBit, v.twinGen);
    parked()[tk] = v.pcm;
    FMOD_CHANNEL* tw = XPLMPlayPCMOnBus(
        v.pcm->muffled.data() + first, bytes, FMOD_SOUND_FORMAT_PCM16,
        v.pcm->rate, 1, 0, busFor(v), channelComplete,
        reinterpret_cast<void*>(static_cast<uintptr_t>(tk)));
    if (tw) {
      v.twin = tw;
      XPLMSetAudioVolume(tw, twinGainFor(v));
    }
    // twin failed clear plays alone
  }
  return true;
}

void Player::haltTwin(Voice& v) {
  if (!v.twin) return;
  FMOD_CHANNEL* tw = static_cast<FMOD_CHANNEL*>(v.twin);
  v.twin = nullptr;
  ++v.twinGen;  // late callback is stale now
  XPLMStopAudio(tw);
}

void Player::haltVoice(Voice& v) {
  haltTwin(v);
  if (!v.channel) return;
  v.held = true;
  FMOD_CHANNEL* ch = static_cast<FMOD_CHANNEL*>(v.channel);
  v.channel = nullptr;
  ++v.gen;
  XPLMStopAudio(ch);
}

void Player::clearVoice(Voice& v) {
  haltVoice(v);
  v.active = v.held = v.done = false;
  v.pcm.reset();
  v.path.clear();
  v.label.clear();
  v.pos = 0;
}

void Player::stopAll() {
  for (Voice& v : voices_) clearVoice(v);
  musicWanted_ = false;
}

void Player::prefetch(AnnType type, CallContext ctx) {
  if (!pack_) return;
  Rng copy = rng_;
  const Selection s = selectSound(*pack_, type, ctxFor(ctx), copy,
                                  lastByType_[static_cast<int>(type)]);
  if (s.file) cache_.request(s.file->path);
}

// purseroutput

std::optional<Mode> Player::peek(AnnType type, CallContext ctx) const {
  if (!pack_) return std::nullopt;
  Rng copy = rng_;
  const auto it = lastByType_.find(static_cast<int>(type));
  const Selection s =
      selectSound(*pack_, type, ctxFor(ctx), copy,
                  it == lastByType_.end() ? std::string() : it->second);
  if (!s.file || cache_.failed(s.file->path)) return std::nullopt;
  return s.file->effectiveMode();
}

std::optional<std::string> Player::play(AnnType type, CallContext ctx,
                                        bool interphone) {
  if (!pack_) return std::nullopt;
  const Selection s = selectSound(*pack_, type, ctxFor(ctx), rng_,
                                  lastByType_[static_cast<int>(type)]);
  if (!s.file) {
    for (const Candidate& c : s.candidates)
      logDebug("  %s: %s", c.file->relPath.c_str(), c.why.c_str());
    return std::nullopt;
  }
  std::string err;
  if (cache_.failed(s.file->path, &err)) {
    logWarn("audio: %s/%s cannot be decoded (%s): skipped", pack_->name.c_str(),
            s.file->relPath.c_str(), err.c_str());
    return std::nullopt;
  }
  if (s.timeFallback || (s.variantFallback && s.variant != 0)) {
    std::string note;
    if (s.timeFallback)
      note += std::string(" nearest time of day (no ") +
              timeOfDayName(base_.time) + " file)";
    if (s.variantFallback && s.variant != 0)
      note += " variant " + std::to_string(s.variant) + " (variant " +
              std::to_string(base_.variant) + " has none)";
    logInfo("%s/%s:%s", pack_->name.c_str(), s.file->relPath.c_str(),
            note.c_str());
  }
  Voice& v = voices_[kPa];
  clearVoice(v);
  v.kind = interphone ? SoundKind::Interphone : SoundKind::Pa;
  v.path = s.file->path;
  v.label = pack_->name + "/" + s.file->relPath;
  v.active = true;
  lastByType_[static_cast<int>(type)] = s.file->path;
  if ((v.pcm = cache_.get(v.path)))
    startVoice(v, 0);  // else update starts it after decode
  if (voices_[kMusic].channel)
    haltVoice(voices_[kMusic]);  // music ducks out under announcements
  return v.label;
}

bool Player::playing() const {
  const Voice& v = voices_[kPa];
  return v.active && !v.done;
}

void Player::stop() { clearVoice(voices_[kPa]); }

void Player::chime() {
  Voice& v = voices_[kChime];
  clearVoice(v);
  v.pcm = chimePcm_;
  v.label = "cockpit call buzzer";
  v.active = true;
  startVoice(v, 0);
}

void Player::setMusic(bool on) {
  musicWanted_ = on;
  if (!on) clearVoice(voices_[kMusic]);
}

void Player::nextMusicTrack() {
  Voice& v = voices_[kMusic];
  clearVoice(v);
  if (!pack_) return;
  Rng& r = rng_;
  const Selection s = selectSound(*pack_, AnnType::BoardingMusic,
                                  ctxFor(CallContext::Any), r, lastMusic_);
  if (!s.file || cache_.failed(s.file->path)) return;
  v.path = s.file->path;
  v.label = pack_->name + "/" + s.file->relPath;
  v.active = true;
  lastMusic_ = s.file->path;
}

// per tick

void Player::update(float dt) {
  if (!set_ || !sim_) return;
  const bool hold = sim_->paused || sim_->replay;
  Voice& pa = voices_[kPa];
  const bool paBusy = pa.active && !pa.done;

  for (Voice& v : voices_) {
    if (v.done && v.twin) haltTwin(v);  // clear ended first twin goes too
    if (!v.active || v.done) continue;
    if (v.channel) {
      if (!hold) v.pos += std::max(0.0f, dt);
      XPLMSetAudioVolume(static_cast<FMOD_CHANNEL*>(v.channel), gainFor(v));
      if (v.slot == kPa) lastPaGain_ = gainFor(v);
    }
    if (v.twin)
      XPLMSetAudioVolume(static_cast<FMOD_CHANNEL*>(v.twin), twinGainFor(v));
  }

  // pause/replay stop remember position resume when the sim runs 4.4
  for (Voice& v : voices_) {
    if (!v.active || v.done) continue;
    if (hold) {
      if (v.channel) haltVoice(v);
      continue;
    }
    if (v.slot == kMusic) continue;
    if (!v.channel) {
      if (!v.pcm) v.pcm = cache_.get(v.path);
      std::string err;
      if (!v.pcm && !v.path.empty() && cache_.failed(v.path, &err)) {
        logWarn("audio: %s cannot be decoded (%s)", v.label.c_str(),
                err.c_str());
        v.done = true;
        continue;
      }
      if (v.pcm) startVoice(v, v.pos);
    }
  }

  // boarding music playlist paused during announcements
  Voice& m = voices_[kMusic];
  if (!musicWanted_) {
    if (m.active) clearVoice(m);
  } else if (!hold) {
    if (!m.active || m.done) nextMusicTrack();
    if (m.active && !m.done) {
      const bool under =
          paBusy || (voices_[kChime].active && !voices_[kChime].done);
      if (under) {
        if (m.channel) haltVoice(m);
      } else if (!m.channel) {
        if (!m.pcm) m.pcm = cache_.get(m.path);
        if (m.pcm)
          startVoice(m, m.pos);
        else if (cache_.failed(m.path))
          m.done = true;
      }
    }
  }

  // finished one-shots release their buffers
  for (Voice& v : voices_)
    if (v.active && v.done && v.slot != kPa && v.slot != kMusic) clearVoice(v);
}

// labels

Player::NowPlaying Player::nowPlaying() const {
  NowPlaying n;
  const Voice& v = voices_[kPa];
  if (!v.active || v.done) return n;
  n.label = v.label;
  n.decoding = !v.pcm;
  n.paused = v.held;
  n.interphone = v.kind == SoundKind::Interphone;
  n.pos = v.pos;
  n.dur = v.pcm ? v.pcm->seconds() : 0;
  return n;
}

float Player::progress() const {
  const Voice& v = voices_[kPa];
  if (!v.active || !v.pcm || v.pcm->seconds() <= 0) return 0;
  return static_cast<float>(std::clamp(v.pos / v.pcm->seconds(), 0.0, 1.0));
}

std::string Player::musicLabel() const {
  const Voice& v = voices_[kMusic];
  if (!v.active || v.done) return {};
  return v.label + (v.channel ? "" : " (paused)");
}

}  // namespace fxa
