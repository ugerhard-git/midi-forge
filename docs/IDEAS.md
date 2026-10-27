# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

(nothing at the moment)

## Ideas

### Step modulator (being designed)

A new modulation source: a sequence of 4 … 16 steps, each with its own value, played in time with the song
and routed like an LFO (knobs and On/Off switches), e.g. for fills, phrase endings and uneven rhythms.
Design in progress, see the discussion of 27 Oct 2026.

### Groove

Pushes notes towards a groove template (MPC-style swing 54–75 %, laid-back snare …) with a *Feel* amount,
so straight programming gets a pocket instead of random wobble. Needs: which templates, how far notes may
move (a plug-in can only delay notes, so "early" means delaying everything else).

### Strum

Plays chords as strums: the notes one after another instead of together. Speed (ms or tempo-synced),
direction (up, down, alternating, random) and a velocity tilt across the strum.

### Beat-aware thinning

Like Probability, but each position in the bar has a weight: downbeats almost always play, off-beat
sixteenths drop out more often. Variation without losing the backbone of the groove.

### Mutate

Now and then replaces a note with a neighbouring note of the scale, so a melody evolves slowly but stays
recognisable. Needs: scale source (own Root/Scale knobs or "follow Scale Quantize"), how often, how far.

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
