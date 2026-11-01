# Changelog

One `## x.y.z` section per version, newest first. The section is pasted into the GitHub release for tag `vx.y.z`,
and the release is refused if it's missing.

## 0.2.0
### Downloads
- First packaged release: one zip with the plugin for Linux, Windows and macOS (Intel and Apple Silicon), ready to
  copy into `Resources/plugins/`.

### Fixes
- The UI no longer calls into X-Plane while the plugin is unloading (0.1.1). Disabling or reloading FXAnnounce no
  longer risks a crash.
- Your old `boarding_on_call` setting is converted to the new **Boarding starts** setting automatically.

### Changes since 0.1
- ToLiss A320 and A321 support, with boarding timed from the SimBrief plan.
- The A330 and A340 load.
- The Inspector and probe checklist for checking ToLiss values on your own aircraft.
