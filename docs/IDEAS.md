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

### Note Split module (proposal sent to the owner, waiting for answers)

A Filter that splits the chain: selected notes continue through the slots below it, all other notes skip
the rest of the chain and go straight to the DAW unchanged. Example: only the hi-hat (F#1) goes on to a
Repeater, kick and snare pass through untouched.

- Proposed knobs: Low, High (same note = exactly one note, the default), Octaves (This octave / Every
  octave), Mode (Selected → chain / Selected → DAW).
- A note-off always takes the same path as its note-on (also after knob changes or LFO modulation); poly
  aftertouch follows its note. Several Note Splits can be chained. Panic and slot changes also release
  notes that skipped the chain.
- Engine: the chain needs a bypass path that merges into the final output and is tracked by the
  stuck-note logic.
- Open questions: one note or a range? "Every octave" and invert mode wanted? Non-note messages down the
  chain or straight to the DAW (or a knob)? Name?

### Six knobs per slot (A–F)

Inspired by the Repeater (0.4.0), which would like adjustable **accent strength** and **note length** besides
rate, velocity source, swing and accent pattern. Other modules could use the extra knobs later too
(e.g. Arpeggiator swing, Note Echo note length).

- 2 more host parameters per slot (16 in total), appended so existing projects stay compatible;
  4 → 6 modulation targets per slot.
- Wider slot rows or a second knob row; check that the window still fits on small laptop screens.
- Existing modules keep their A–D; E and F stay unused until a module needs them.

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).
