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
3. Strum + Ghost Notes (0.18)
4. Beat-aware thinning + Diatonic Harmonizer (0.19), with a shared scale helper
5. Modulating the modulation (0.20)
6. Pitch Slide: Fall Off (0.21)
- Later, separately: Pitch Slide MPE. Parked until the owner wants it: macOS and CLAP builds.

## Planned

(nothing at the moment)

## Ideas

### Strum

Plays chords as strums: the notes one after another instead of together. Speed (ms or tempo-synced),
direction (up, down, alternating, random) and a velocity tilt across the strum.

### Beat-aware thinning

Like Probability, but each position in the bar has a weight: downbeats almost always play, off-beat
sixteenths drop out more often. Variation without losing the backbone of the groove.

### Ghost Notes

Adds quiet extra hits shortly before or after notes, with a chance, mainly for drums (hi-hats, snares).

### Diatonic Harmonizer

Adds a harmony note (third, fifth, sixth … above or below) that stays in key, unlike the fixed intervals of
Chord. Needs: Root/Scale knobs, interval in scale steps, one or two voices.

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).

### macOS and CLAP builds

Switched off in 0.9.0 because only the Windows VST3 is needed for now. CMake still builds a universal binary
on macOS, and `-DMIDI_FORGE_BUILD_CLAP=ON` brings CLAP back. To return: add the macOS job to the CI matrix
again (ad-hoc `codesign`, `ditto` zip), add the CLAP target and file to the build and collect steps, and put
the install folders and the macOS quarantine note back into the manual and packaging/INSTALL.txt.

### Pitch Slide: Fall Off on release and MPE

Two follow-ups discussed while planning the Pitch Slide (0.10.0): a *Fall Off* mode that starts the slide when
the key is released (keeping the note sounding for the slide length), and an MPE mode that gives every note
its own channel so notes of a chord can slide individually.
