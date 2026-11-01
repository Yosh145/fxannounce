# FXAnnounce: instructions for AI agents

FXAnnounce is a GPL-3.0 native X-Plane 12 plugin that plays Fenix-format *Airline Pack* cabin announcements on the
ToLiss Airbus family, driven automatically by the flight (a "Virtual Purser"). Agents write the code and docs. The
owner tests in X-Plane and reviews.

1. **The Fenix guides are the reference.** `docs/VP_SPEC.md` restates them and maps them onto ToLiss. Where the spec
   and Fenix disagree, Fenix wins unless the difference is a listed deviation (D-n). New deviations need the owner's
   approval.
2. **Spec before code.** Change `docs/VP_SPEC.md` (behaviour) or `docs/ARCHITECTURE.md` (structure) first, then the
   code, in the same session. Verified ToLiss dataref encodings go in `docs/TOLISS_DATAREFS.md`.
3. **Crash safety is a hard rule** (`docs/ARCHITECTURE.md` § 4): every XPLM callback goes through `fxa::guard`,
   no XPLM calls off the main thread, no writes to ToLiss datarefs or commands in v1, `core/` stays free of XPLM
   and ImGui.
4. **Versioning:** semver in `native/CMakeLists.txt` (`project(FXAnnounce VERSION ...)`). Bump it in the same commit
   as every change under `native/src/` or `native/data/`, and add or extend that version's `## x.y.z` section in
   `CHANGELOG.md` (user-facing wording: it becomes the release notes). Releases come from pushing tag `vx.y.z`
   (`docs/ARCHITECTURE.md` § 6); the owner tags and publishes, never an agent.
5. **Build and tests:**
   `cmake -S native -B build -G Ninja && cmake --build build && cmake --build build --target fx_tests fx_ui_tests && ./build/fx_tests && ./build/fx_ui_tests`
6. **Never commit** `Packs/` (community audio), the downloaded Fenix pages, or `old/`. Tests that need the sample
   packs skip when they're absent.
7. **Never write into the X-Plane install** (`/mnt/data/X-Plane 12`) or the ToLiss aircraft folders without the
   owner's explicit OK. Read-only inspection is fine.
8. In-sim acceptance is the owner's job. Never mark it passed yourself.
