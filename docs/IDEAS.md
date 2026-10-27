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

### LFO polarity: bipolar or unipolar

The owner asked for LFOs that affect values both positively and negatively ("through zero"). Today every
LFO is already bipolar: it swings the parameter above *and* below the knob's position (output −1 … +1,
and a negative amount inverts it). Open question for the owner: is the bipolar behaviour what was meant,
or is the wish an LFO **polarity switch** per LFO (Bipolar −1 … +1 / Unipolar 0 … +1, so the LFO only pushes
the value up from the knob position, or only down with a negative amount)? A switch would be one new
host parameter per LFO (appended) plus a toggle in each LFO panel.

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
