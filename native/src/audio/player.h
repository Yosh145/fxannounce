// announcements music and buzzer thru xplmplaypcmonbus main thread only
// architecture 3 and 4.6
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

#include "audio/decode_cache.h"
#include "audio/hearing.h"
#include "core/pack_library.h"
#include "core/purser.h"
#include "core/selector.h"

namespace fxa {

class Player : public PurserOutput {
 public:
  explicit Player(DecodeCache& cache);
  ~Player() override;
  Player(const Player&) = delete;
  Player& operator=(const Player&) = delete;

  // refreshed every tick
  void setSource(const Pack* pack, const SelectContext& base,
                 const Settings* set, const SimState* sim, bool muted);
  // volumes pause/resume decode waits music playlist
  void update(float dt);
  void stopAll();
  void prefetch(AnnType type, CallContext ctx);

  // ui
  struct NowPlaying {
    std::string label;  // empty when nothing plays
    bool decoding = false, paused = false;
    bool interphone = false;  // interphone not cabin pa
    double pos = 0, dur = 0;  // secs
  };
  NowPlaying nowPlaying() const;
  float progress() const;
  std::string musicLabel() const;
  float lastGain() const { return lastPaGain_; }

  // key slot
  // twin bit
  // generation 3
  void onChannelDone(uint64_t key);

  std::optional<std::string> play(AnnType type, CallContext ctx,
                                  bool interphone) override;
  bool playing() const override;
  void stop() override;
  void chime() override;
  void setMusic(bool on) override;
  std::optional<Mode> peek(AnnType type, CallContext ctx) const override;

 private:
  struct Voice {
    int slot = 0;
    SoundKind kind = SoundKind::Pa;
    std::shared_ptr<PcmBuffer> pcm;
    std::string path, label;
    void* channel = nullptr;  // fmod_channel
    unsigned gen = 0;         // stale callbacks ignored
    void* twin = nullptr;     // muffled twin or null 4.5
    unsigned twinGen = 0;
    double pos = 0;       // secs played
    bool active = false;  // playing waiting on decode or paused
    bool held = false;    // we stopped it to resume later
    bool done = false;    // ended or failed
  };
  enum Slot { kPa = 0, kMusic = 1, kChime = 2, kSlots = 3 };
  // slot 2 bits twin bit generation 3
  static constexpr int kTwinBit = 4;

  bool startVoice(Voice& v, double from);
  void haltVoice(Voice& v);  // keeps the position
  void haltTwin(Voice& v);
  bool wantsTwin(const Voice& v) const;
  float twinGainFor(const Voice& v) const;
  void clearVoice(Voice& v);
  int busFor(const Voice& v) const;
  float gainFor(const Voice& v) const;
  void nextMusicTrack();
  SelectContext ctxFor(CallContext c) const;

  DecodeCache& cache_;
  Voice voices_[kSlots];
  const Pack* pack_ = nullptr;
  SelectContext base_;
  const Settings* set_ = nullptr;
  const SimState* sim_ = nullptr;
  bool muted_ = false;
  bool musicWanted_ = false;
  std::string lastMusic_;
  std::map<int, std::string> lastByType_;
  mutable Rng rng_{12345};
  std::shared_ptr<PcmBuffer> chimePcm_;
  float lastPaGain_ = 0;
};

// buffers whose completion never came xpluginstop only
void releaseParkedAudio();

}  // namespace fxa
