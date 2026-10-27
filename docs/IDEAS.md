# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### Paged knobs (up to 8 per slot), Repeater page 2, stepped knobs

Design agreed; waiting for the go. Built together because all three change the knob code.

**Paged knobs**
- Four knobs stay on screen; modules with more than four parameters show **◀ ▶ arrows** and a small "1/2"
  indicator next to the knobs. The window size does not change.
- Every slot gets host parameters E–H (32 in total, appended); modules declare up to 8 parameters,
  existing modules keep A–D.
- Route targets for E–H get new numbers after the existing ones, so stored routes keep their knobs; the
  target menu lists everything sorted by slot. Presets and user preset files without E–H get defaults.
- The page shown per slot is remembered in projects and presets; switching the module goes back to page 1.
- A yellow dot on the arrows marks a knob on the hidden page that an LFO or Random modulator moves.

**Repeater page 2:** *Accent strength* (replaces the fixed +25 velocity) and *Note length* (replaces the
fixed 50 % of a step).

**Stepped knobs:** knobs whose parameter has fixed steps (choices, whole numbers) jump from step to step
while dragging; the mouse wheel moves one step per notch. Applies to the slot knobs and to the LFO and
Random knobs with steps. Host automation is unchanged (the modules round anyway).

**Tests:** E–H parameters and their position at the end, target numbering compatibility (stored routes),
loading 0.6.0 projects and user presets, page memory in state and presets, Repeater accent strength and
note length incl. the stress test, slider step intervals for every parameter kind.

## Ideas

### Repeater: Chance knob

A *Chance* knob for the Repeater: each repeat plays only with this probability (0 … 100 %), so ratchets get
gaps and feel less mechanical. Needs a fifth knob, so it fits on the Repeater's page 2 once paged knobs
exist. To discuss: does the first, immediate hit always play (suggested) or is it subject to the chance
as well? Should accented steps have their own chance?

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).
