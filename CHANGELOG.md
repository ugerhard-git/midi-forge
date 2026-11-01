# Changelog

What changed in each release of MIDI Forge. The release workflow publishes the section of a version as the
text of its GitHub release, so every section starts with `## [version]` and is written for users.
Versions below 1.0 are betas: they are published as pre-releases.

## [0.25.0] – CC Out, Bounce, CLAP and macOS

The first release since 0.22.1, with everything built since then (0.23.0 and 0.24.0 were not published on
their own).

### New

- **CC Out** (a new Generator): turns modulation into sound. It sends a MIDI controller – or pitch bend, or
  channel pressure – to the synth. Point a modulation route at its *Value*, and an LFO, a Random or a step
  modulator moves the synth's filter cutoff, volume or any other controller, not just MIDI Forge's own knobs.
  *Low* and *High* set the range, *Smoothing* takes the edge off jumps, and *Send* decides whether it sends
  always, only while the DAW plays (the default) or only while keys are held. Incoming messages of the same
  controller are blocked, so they never fight. New factory preset **Breathing Cutoff**: a 2-bar LFO sweeps
  CC 74, on many synths the filter cutoff.
- **Bounce** (a new Generator): a delay whose repeats follow a ball bouncing on the floor. The note you play is
  the first impact, then every bounce comes sooner and softer than the one before. Pick the drop height
  (5 cm … 5 m), the ball's material (Super Ball … Clay) and the gravity (Earth, Moon, Mars, Jupiter, Pluto,
  Sun) – or let the first gap follow the tempo and the rest speed up freely. Bounces can climb in pitch, and
  you can limit how many there are.
- **Note Echo: Pre-Delay** (page 2): a pause before the first echo, as a note value (1/64 … 1 Bar) or in
  milliseconds (1 … 1000 ms). The note itself still plays right away. Projects and presets from older
  versions load with Pre-Delay off, so they sound as before.
- **CLAP**: MIDI Forge now also comes as a CLAP plug-in, for Windows and macOS. Reaper and Bitwig Studio list
  it next to the VST3; both work the same.
- **macOS**: VST3, CLAP and an **Audio Unit** for Logic Pro's MIDI FX slot, as universal binaries for Apple
  Silicon and Intel (macOS 11 or newer). See *Known limitations*: not tried on a Mac yet.

### Fixed

- In CLAP hosts, typing a route target that contains "·" (e.g. "Slot 3 · A (unused)") selected *None*.
- With very many notes echoing at once (thousands of pending echoes), Note Echo could end another note of the
  same pitch too early.

### Downloads

- `MIDI-Forge-Windows.zip` – VST3 and CLAP for Windows (64-bit).
- `MIDI-Forge-macOS.zip` – VST3, CLAP and Audio Unit for macOS. It may appear on this page a few minutes
  after the Windows zip.

Every zip contains `INSTALL.txt` (where each format goes), the licence and `THIRD-PARTY-NOTICES.txt` with the
licences of the third-party code inside the plug-ins (VST3 SDK, CLAP, Apple's Audio Unit SDK). Installation:
see `INSTALL.txt`, or the [user manual](https://github.com/ugerhard-git/midi-forge/blob/main/docs/MANUAL.md#installation).

### Known limitations

- **macOS: not tried on a Mac yet.** The build is checked automatically – signatures, both processor types,
  Apple's Audio Unit validator and the CLAP validator – but no one has played it in a DAW on a Mac so far.
  Please report how it works in the *Issues*.
- The macOS plug-ins are not notarized by Apple: after copying them, unblock them once in the Terminal as
  `INSTALL.txt` describes.
- Made and tested for Reaper and Bitwig Studio on Windows; other hosts with MIDI effect support should work.
- Pitch Slide bends a whole MIDI channel; notes of a chord slide together (no MPE yet).

## [0.22.1] – Diatonic Harmonizer is a Generator

- The **Diatonic Harmonizer** now belongs to the *Generators* (orange), next to Chord: it adds notes to the
  ones you play. Until now it was listed under *Transforms* (green). Projects and presets keep their
  Harmonizer with all its settings; only its colour and its place in the module browser change.
- The user manual gives every module its own section, with an overview table of all modules at the top.

## [0.22.0] – first public beta

MIDI Forge is a modular MIDI effect for your DAW: eight slots, each holding one module, process the MIDI on
its way to your instrument – filter, transform, generate, randomize – and four kinds of modulation move the
modules' settings over time. It makes no sound itself; it goes in front of an instrument.

This is the first public release. Everything planned for it is in place and tested automatically, but it has
not been used by many people yet: please report problems and ideas in the repository's *Issues*.

### What's in it

- **24 modules in four groups**
  - *Filters:* Note Range, Velocity Range, Channel Filter, Event Blocker, Note Split
  - *Transforms:* Transpose, Scale Quantize, Velocity Curve, Channel Remap, Groove, Diatonic Harmonizer,
    Note Dispatcher
  - *Generators:* Arpeggiator, Chord, Note Echo, Repeater (ratchets), Strum, Ghost Notes
  - *Randomizers:* Humanize, Probability, Random Pitch, Beat Thinning, Mutate, Pitch Slide (with Fall Off)
- **Modulation:** 4 LFOs, 4 Random modulators (a new value per note or phrase), 4 step modulators (4–16
  drawn steps, with ties and glide) and 12 routes. Routes move any module knob, switch slots on and off, and
  can even move the modulation itself (another route's amount, an LFO's rate, phase or width, a step
  modulator's rate or glide).
- **Follows your DAW:** tempo, song position and time signature (4/4, 3/4, 6/8, 7/8 …) – grooves, bar-long
  settings and the beat-aware modules use the real bar.
- **No stuck notes**, even when you change, move or switch modules while playing; a Panic button for
  everything else.
- **Presets:** 12 factory presets and your own, saved as files you can back up and share.
- **Module browser** with search, recently used modules and built-in help for every module and knob.

### Downloads

- `MIDI-Forge-Windows.zip` – VST3 for Windows (64-bit).

Installation: see `INSTALL.txt` in the zip, or the [user manual](https://github.com/ugerhard-git/midi-forge/blob/main/docs/MANUAL.md#installation).

### Known limitations

- Windows only for now; a macOS build is planned.
- VST3 only: no Audio Unit and no CLAP for now.
- Made and tested for Reaper and Bitwig Studio; other hosts with MIDI effect support should work.
- Pitch Slide bends a whole MIDI channel; notes of a chord slide together (no MPE yet).

MIDI Forge is free software under the GNU AGPLv3.
