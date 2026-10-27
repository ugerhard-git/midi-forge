# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### Remove a module with one click

Agreed with the owner, waiting for the go.

- A small **✕** button next to the **i** button of every slot that holds a module (not on empty slots).
- One click removes the module: the modules below move up one slot, the empty slot goes to the end of the
  chain.
- Modulation routes follow their modules when they move up; routes to the removed module are set to *None*.
  The ▲ / ▼ buttons get the same behaviour (today routes stay with the slot number when modules move).
- An **undo strip** ("Removed *Repeater* · Undo") shows for a few seconds; Undo puts the module back exactly
  as it was: slot, knobs, power switch, shown page and its routes.

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
