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
- Built in 0.22: the forge look of the header. Built in 0.24: the macOS build (with Audio Unit) and CLAP.
  Parked: Pitch Slide MPE.

## Planned

(nothing at the moment)

## Ideas

### Notarized macOS build

Since 0.24.0 the macOS plug-ins are signed ad hoc only, so users remove the quarantine flag after
downloading (`xattr -dr com.apple.quarantine …`). With an Apple Developer account (99 USD a year), CI could
sign them with a Developer ID certificate and have Apple notarize them: the downloads would then install
without the Terminal step. Needs the certificate and an app-specific password as repository secrets.

### CC Out: 14-bit controllers

Discussed while planning CC Out (0.25.0): send controllers 0–31 with their fine partner (32–63) for 16 384
steps instead of 128. Few synths read it, so CC Out sends 7-bit controllers (pitch bend is 14-bit already).

### Pitch Slide: MPE

Discussed while planning the Pitch Slide (0.10.0): an MPE mode that gives every note its own channel, so
notes of a chord can slide (and fall off) individually.
