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

### Knobs for discrete options snap to the options

Knobs whose parameter has fixed steps (choices like Scale or Rate, whole numbers like Octaves or note
numbers) should jump from step to step while dragging, instead of moving smoothly with the text jumping.
Applies to the slot knobs (A–D) and the LFO knobs (Rate in Tempo mode, Phase steps). Things to decide:
whether host automation should also be stepped (it can stay continuous; the module rounds anyway), and
how mouse-wheel and fine-drag (Shift) behave on stepped knobs.

### Paged knobs: up to 8 per slot (proposal sent to the owner, waiting for answers)

Replaces the earlier "six knobs per slot" idea. Inspired by the Repeater (0.4.0), which would like
adjustable **accent strength** and **note length**.

- Four knobs stay on screen; modules with more than four parameters get a second page (proposed: a small
  "1 | 2" switch next to the knobs, shown only for such modules). The window size does not change.
- Every slot gets host parameters E–H (32 in total, appended); modules declare up to 8 parameters,
  existing modules keep A–D.
- Route targets for E–H get new numbers after the existing ones, so stored routes keep their knobs; the
  target menu still lists everything sorted by slot. Presets and user preset files without E–H get defaults.
- Switching the module goes back to page 1; a dot on the page switch marks a modulated knob on the hidden page.
- Candidates for page 2: Repeater accent strength and note length; Arpeggiator swing; Chord strum; Note Echo
  note length (each to be discussed on its own).
- Open questions: "1 | 2" or arrows? Remember the page in projects/presets? First build = infrastructure +
  Repeater accent strength and note length? Dot for modulated hidden knobs?

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).
