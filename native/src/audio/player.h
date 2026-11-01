// Plays the purser's announcements, boarding music, the cabin chime and previews through X-Plane's FMOD
// (XPLMPlayPCMOnBus). Main thread only (ARCHITECTURE.md § 3, § 4.6).
#pragma once

#include "audio/decode_cache.h"
#include "audio/hearing.h"
#include "core/pack_library.h"
#include "core/purser.h"
#include "core/selector.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace fxa {

class Player : public PurserOutput {
public:
    explicit Player(DecodeCache& cache);
    ~Player() override;
    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    // What to select from and how loud, refreshed every tick by the plugin.
    void setSource(const Pack* pack, const SelectContext& base, const Settings* set, const SimState* sim, bool muted);
    // Per tick: volumes, pause/resume, starts waiting for a decode, music playlist.
    void update(float dt);
    void stopAll();
    void prefetch(AnnType type, CallContext ctx);

    void preview(const std::string& path, const std::string& label);
    void stopPreview();
    bool previewing() const;
    std::string previewLabel() const;

    // UI
    std::string nowPlaying() const;        // "DLH/ArmDoors.ogg 0:12 / 0:31"
    float progress() const;                // 0..1 of the current announcement
    std::string musicLabel() const;
    float lastGain() const { return lastPaGain_; }

    // X-Plane's completion callback for one channel (key = slot | generation << 3).
    void onChannelDone(uint64_t key);

    // PurserOutput
    std::optional<std::string> play(AnnType type, CallContext ctx, bool interphone) override;
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
        void* channel = nullptr;     // FMOD_CHANNEL*
        unsigned gen = 0;            // generation of the current channel (stale callbacks are ignored)
        double pos = 0;              // seconds played
        bool active = false;         // wanted (playing, waiting for a decode, or paused)
        bool held = false;           // stopped by us to resume later (pause, music under PA)
        bool done = false;           // finished naturally or failed
    };
    enum Slot { kPa = 0, kMusic = 1, kChime = 2, kPreview = 3, kSlots = 4 };

    bool startVoice(Voice& v, double from);
    void haltVoice(Voice& v);         // stops the channel, keeps the position
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
    mutable Rng rng_{ 12345 };
    std::shared_ptr<PcmBuffer> chimePcm_;
    float lastPaGain_ = 0;
};

// Releases buffers still parked for channels whose completion never arrived. Call at XPluginStop.
void releaseParkedAudio();

} // namespace fxa
