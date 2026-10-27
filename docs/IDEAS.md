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
