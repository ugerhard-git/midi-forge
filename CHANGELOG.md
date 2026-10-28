# Changelog

What changed in each release of MIDI Forge. The release workflow publishes the section of a version as the
text of its GitHub release, so every section starts with `## [version]` and is written for users.
Versions below 1.0 are betas: they are published as pre-releases.

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
- `MIDI-Forge-macOS.zip` – VST3 for macOS 11 or newer (Apple Silicon and Intel).

Installation: see `INSTALL.txt` in the zip, or the [user manual](https://github.com/ugerhard-git/midi-forge/blob/main/docs/MANUAL.md#installation).

### Known limitations

- **macOS:** this is the first macOS build and has not been tried in a DAW yet – feedback is very welcome.
  It is not notarized by Apple, so macOS blocks it after the download until you remove the quarantine flag
  (one Terminal command, see `INSTALL.txt`).
- VST3 only: no Audio Unit (so not for Logic Pro) and no CLAP for now.
- Made and tested for Reaper and Bitwig Studio; other hosts with MIDI effect support should work.
- Pitch Slide bends a whole MIDI channel; notes of a chord slide together (no MPE yet).

MIDI Forge is free software under the GNU AGPLv3.
