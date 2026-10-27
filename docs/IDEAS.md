# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### LFO polarity per route (Bipolar / Unipolar)

Design agreed; waiting for the go. Background: LFOs are bipolar today (−1 … +1, swinging the value below
*and* above the knob position). For knobs at the bottom of their range (Inversion 0, Octaves 1,
Repeats 1 …) the lower half is cut off at the minimum, so half the LFO is wasted.

- A **Polarity** switch per **route**: *Bipolar* (−1 … +1, today's behaviour, the default) or *Unipolar*
  (0 … +1): the route only pushes the value up from the knob position, or only down with a negative amount.
  One LFO can drive a bipolar route and a unipolar route at the same time.
- On unipolar routes the cycle starts at the **lowest point**: Sine and Triangle are read a quarter cycle
  earlier (Saw Up, Square and the random waves already start low or have no fixed start). Together with
  *Restart on key*, every phrase starts at the knob position and rises. Bipolar routes of the same LFO
  are unaffected.
- One new automatable host parameter per route (appended, so existing projects keep bipolar routes) and a
  toggle in each route row on the Modulation tab; the knob rings show the unipolar range.
- Tests: unipolar range and sign with positive/negative amounts, lowest-point start with restart on key,
  mixed bipolar/unipolar routes on one LFO, state and presets, stress test with random polarities.

## Ideas

### Note Random modulators (proposal sent to the owner, waiting for answers)

Modulation sources that roll a random value (−1 … +1) on each new played note and hold it until the next
one. They appear in the routes' Source menu after the LFOs (Random 1, Random 2) and can drive every knob
the LFOs can, with the same amount and (once built) per-route polarity.

- Chord window: the first note-on rolls a new value immediately (no latency) and already uses it; further
  note-ons within the window (proposed 0 … 100 ms, default 30 ms) keep that value, so a slightly uneven
  chord counts as one event.
- Proposed settings per modulator: Chord window; New value on (Every note / First note of a phrase);
  Steps (Smooth, 2 … 12).
- Only notes played into the plug-in trigger; generated notes (arpeggio, repeats, echoes) use the current
  value. The engine already splits blocks at every input event, so values apply exactly from that note.
- Compatibility: the route Source parameter gets two more choices; project state stores the index, only
  automation of a route's Source could shift.
- Open questions: how many modulators? window range/default? phrase option? steps? generated notes
  trigger or not?

### Knobs for discrete options snap to the options

Knobs whose parameter has fixed steps (choices like Scale or Rate, whole numbers like Octaves or note
numbers) should jump from step to step while dragging, instead of moving smoothly with the text jumping.
Applies to the slot knobs (A–D) and the LFO knobs (Rate in Tempo mode, Phase steps). Things to decide:
whether host automation should also be stepped (it can stay continuous; the module rounds anyway), and
how mouse-wheel and fine-drag (Shift) behave on stepped knobs.

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
