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

### Note Random modulators (4)

Design agreed; waiting for the go. Modulation sources that roll a random value on each new played note
and hold it until the next one. They appear in the routes' Source menu after the LFOs (**Random 1 … 4**)
and can drive every knob the LFOs can, with the same amount (±100 %) and the per-route polarity above.

- **Chord window** (0 … 100 ms, default 30 ms): the first note-on rolls a new value immediately (no
  latency) and already uses it; further note-ons within the window keep that value, so a slightly uneven
  chord counts as one event.
- **New value on:** *Every note* or *First note of a phrase* (only when a key is pressed after all keys
  were released, for legato playing).
- **Steps:** *Smooth* (any value) or 2 … 12 evenly spaced values (good for choice knobs like Scale or
  Chord type).
- Only notes **played into the plug-in** trigger a new value; notes the modules generate (arpeggio steps,
  repeats, echoes) use the current value. The engine already splits blocks at every input event, so a new
  value applies exactly from the note that rolled it.
- Host parameters per modulator (appended): chord window, trigger mode, steps. The route Source parameter
  gets four more choices; project state stores the index, so projects stay correct (only existing
  automation of a route's Source could shift).
- UI: a "Random" row on the Modulation tab with four compact panels (current value, settings); rings on
  modulated knobs work as for LFOs.
- Tests: new value per note, chord window boundaries, phrase mode, steps quantisation, generated notes
  don't trigger, routes with both polarities, state and presets, stress test with random routings.

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
