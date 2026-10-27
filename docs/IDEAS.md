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

### Repeater (ratchet) module (proposal sent to the owner, waiting for answers)

A Generator module that retriggers every held note on a tempo grid, for ratchet effects.

- All held notes repeat together on each step; steps are locked to the host grid while the transport runs,
  otherwise run at project tempo (like the Arpeggiator). Releasing a key stops its repeats cleanly.
- Velocity of the repeats: *Last Note* (velocity of the most recently pressed key) or *Aftertouch*
  (poly pressure per note if sent, else channel pressure; note velocity until pressure arrives).
  Aftertouch still passes through.
- Swing on every second step (50 % straight … 75 % heavy); accent on selected steps.
- Proposed knobs (slots have four): Rate (1/4 … 1/64 incl. T and D), Velocity (Last Note / Aftertouch),
  Swing, Accent (Off, every 2nd/3rd/4th step, On the beat); fixed accent strength (+25) and note length (50 %).
- Alternative: six knobs per slot (A–F) for adjustable accent strength and note length.
- Open questions: chord ratchet ok? first hit immediate or on the grid? 4 or 6 knobs? accent patterns?
  a third velocity choice "each note keeps its own velocity"?


### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).
