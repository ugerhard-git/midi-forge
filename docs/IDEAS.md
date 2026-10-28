# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Roadmap

Agreed order (28 Oct 2026). Each step: design discussion, explicit go, build.

1. ~~Step modulator ties (0.16)~~ built
2. ~~Groove (0.17), with a shared "position in the bar" helper~~ built
3. ~~Strum + Ghost Notes (0.18)~~ built
4. ~~Beat Thinning + Diatonic Harmonizer (0.19), time signatures followed everywhere~~ built
5. ~~Modulating the modulation (0.20)~~ built
6. ~~Pitch Slide: Fall Off (0.21)~~ built
- Built in 0.22: the forge look of the header. Parked: the macOS build (see below; GitHub had no Mac runners
  available on release day), Pitch Slide MPE, CLAP build.

## Planned

(nothing at the moment)

## Ideas

### macOS build

Added and removed again in 0.22.0: GitHub's macOS runners were not available (jobs cancelled after 15 minutes
in the queue, "capacity constraints"), and the owner did not want the release to wait. To bring it back,
add to the `build` job's matrix `- name: macOS` / `os: macos-latest`, and after "Collect plug-ins":
ad-hoc sign and check (`codesign --force --deep --sign -`, `codesign --verify --deep`, `lipo -info`), then
zip with `ditto -c -k --sequesterRsrc` in the job and upload that zip as the artifact (and add a download
step for `MIDI-Forge-macOS` in the release job) (a GitHub artifact
loses the bundle's file permissions, which breaks the signature); the release job then takes the zip as it
is. Put the macOS install folder (`~/Library/Audio/Plug-Ins/VST3/`) and the quarantine note
(`xattr -dr com.apple.quarantine …`) back into the manual and packaging/INSTALL.txt. Consider an Intel macOS
runner as a fallback when Apple Silicon runners are scarce (the universal binary builds on either).

### CLAP build

Switched off in 0.9.0. `-DMIDI_FORGE_BUILD_CLAP=ON` brings it back; then add the CLAP target and file to the
build and collect steps of the workflow (and sign it on macOS), and add its install folders to the manual and
packaging/INSTALL.txt.

### Pitch Slide: MPE

Discussed while planning the Pitch Slide (0.10.0): an MPE mode that gives every note its own channel, so
notes of a chord can slide (and fall off) individually.
