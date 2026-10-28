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
4. Beat Thinning + Diatonic Harmonizer (0.19), with a shared scale helper and a time-signature-aware bar
   helper; Groove, Ghost Notes and Mutate fixed to follow the time signature
5. Modulating the modulation (0.20)
6. Pitch Slide: Fall Off (0.21)
- Later, separately: Pitch Slide MPE. Parked until the owner wants it: macOS and CLAP builds.

## Planned

### Time-signature-aware bar position (0.19, shared)

`BlockContext` gets the host's time signature (`PositionInfo::getTimeSignature()`) and the start of the
current bar (`getPpqPositionOfLastBarStart()`). A shared helper turns a sample position into "position in the
bar" and "beat weight" for any meter; without host information it falls back to 4/4. Existing modules that
assume 4/4 today are fixed to use it:

- **Groove**: the Laid-back Snare template delays the backbeats of the current meter (beats 2 and 4 in 4/4,
  beat 3 in 6/8 …) and the template repeats per bar of the real length.
- **Ghost Notes**: "Before" remembers the last bar using its real length.
- **Mutate**: the per-position memory (4 Bars / 2 Bars / 1 Bar) uses the real bar length.

### Beat Thinning (Randomizer, 0.19)

Like Probability, but each position in the bar has a weight from the meter: bar start almost always plays,
other beats less, off-beat subdivisions drop out most. Knobs: **Amount**, **Grid** (1/8, 1/16, 1/32) and
**Grouping** for odd meters (e.g. 7/8 as 2+2+3, 3+2+2 or 2+3+2; ignored where the meter has one natural
grouping). Normal weighting only. A chord is kept or dropped as a whole.

### Diatonic Harmonizer (Transform, 0.19)

Adds up to two harmony notes that stay in key. Knobs: **Root**, **Scale** (shared scale helper with Mutate),
**Voice 1**, **Voice 2** (Off or an interval in scale steps, above or below), **Velocity** of the added
voices. Works on single notes only; chords pass through untouched.

## Ideas

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
