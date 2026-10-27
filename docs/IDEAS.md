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

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).

### macOS and CLAP builds

Switched off in 0.9.0 because only the Windows VST3 is needed for now. CMake still builds a universal binary
on macOS, and `-DMIDI_FORGE_BUILD_CLAP=ON` brings CLAP back. To return: add the macOS job to the CI matrix
again (ad-hoc `codesign`, `ditto` zip), add the CLAP target and file to the build and collect steps, and put
the install folders and the macOS quarantine note back into the manual and packaging/INSTALL.txt.
