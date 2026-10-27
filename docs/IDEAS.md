# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### Repeater (ratchet) module

A Generator module that retriggers every held note on a tempo grid, for ratchet effects. Design agreed;
waiting for the go.

- **All held notes repeat together** on each step (a held chord ratchets as a chord).
- The **first hit plays immediately** when a key is pressed; the repeats are **quantized** to the grid.
  The grid follows the host while the transport runs and runs at project tempo when it is stopped
  (like the Arpeggiator). Releasing a key stops its repeats cleanly.
- **Four knobs:**
  - *Rate*: 1/4 … 1/64, incl. triplets (T) and dotted (D)
  - *Velocity*: *Last Note* (all repeats use the velocity of the most recently pressed key) or *Aftertouch*
    (poly pressure per note if the keyboard sends it, otherwise channel pressure; the note's velocity until
    pressure arrives). Aftertouch messages still pass through.
  - *Swing*: delays every second step, 50 % (straight) … 75 % (heavy shuffle)
  - *Accent*: Off, every 2nd step, every 3rd step, every 4th step, On the beat
- Fixed for now: accent strength +25 velocity, note length 50 % of a step (see "Six knobs per slot").
- Tests: grid lock and free running, immediate first hit, chord repeats, release stops repeats without
  stuck notes, both velocity sources incl. poly and channel pressure, swing timing, every accent pattern,
  stress test includes the module.

## Ideas

### Six knobs per slot (A–F)

Inspired by the Repeater, which would like adjustable **accent strength** and **note length** besides
rate, velocity source, swing and accent pattern. Other modules could use the extra knobs later too
(e.g. Arpeggiator swing, Note Echo note length).

- 2 more host parameters per slot (16 in total), appended so existing projects stay compatible;
  4 → 6 modulation targets per slot.
- Wider slot rows or a second knob row; check that the window still fits on small laptop screens.
- Existing modules keep their A–D; E and F stay unused until a module needs them.

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).
