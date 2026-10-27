# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### Note Dispatcher module (Transform)

Agreed with the owner. Sends incoming notes to a range of MIDI channels, rotating and keeping track of
which channels are free (e.g. to play several mono synths polyphonically, or alternate between sounds).

- **Knobs:** A *Algorithm* (Round Robin / Random / Ping Pong), B *Mode* (Single Notes / Chords),
  C *First Channel* (1–16), D *Channels* (2–16; the range ends at channel 16), page 2: E *Input* (All, 1–16:
  only notes from this channel are dispatched, everything else passes untouched), F *Chord Window*
  (0–100 ms, default 30 ms).
- A channel is **free** when no note sent to it is still held. *Round Robin* takes the next free channel above
  the last one used, wrapping around; *Random* picks any free channel; *Ping Pong* runs up and down without
  repeating the ends (1 2 3 4 3 2 1 2 …).
- **Chords mode:** notes within the Chord Window count as one chord and all go to one channel.
- **All channels busy:** the oldest note (or chord) is ended and its channel reused (steal).
- Note-offs and polyphonic aftertouch follow their note's channel. Pitch bend, CCs, channel aftertouch and
  program changes from the input channel are copied to every target channel.

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
