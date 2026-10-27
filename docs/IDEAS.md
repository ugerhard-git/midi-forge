# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### Pitch Slide module (Randomizer)

Agreed with the owner, waiting for the go. Slides the pitch of incoming notes with pitch bend messages.

- **Knobs:** A *Offset* (−24 … +24 semitones, fixed), B *Mode* (Slide In / Slide Out), C *Length*
  (tempo-synced, 1/64 … 1 bar incl. triplets and dotted), D *Chance* (0 … 100 %, does a note slide at all),
  page 2: E *Curve* (−100 … +100, 0 = linear; negative = fast start, positive = slow start),
  F *Bend Range* (1 … 48 semitones, default 2, must match the synth's pitch bend range).
- **Slide In:** the note starts at Offset and glides to its own pitch. **Slide Out:** the note starts at its own
  pitch and glides to Offset, starting at the note-on.
- Pitch bend acts on the whole channel, so a chord bends together; notes within 30 ms count as one chord and
  get one slide. No MPE for now.
- The player's own pitch bend is added to the slide. About one bend message per millisecond; the bend returns
  to centre when the notes end. Offsets beyond the Bend Range are limited to it.

## Ideas

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).

### macOS and CLAP builds

Switched off in 0.9.0 because only the Windows VST3 is needed for now. CMake still builds a universal binary
on macOS, and `-DMIDI_FORGE_BUILD_CLAP=ON` brings CLAP back. To return: add the macOS job to the CI matrix
again (ad-hoc `codesign`, `ditto` zip), add the CLAP target and file to the build and collect steps, and put
the install folders and the macOS quarantine note back into the manual and packaging/INSTALL.txt.
