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

### Repeater: Chance knob

A *Chance* knob for the Repeater: each repeat plays only with this probability (0 … 100 %), so ratchets get
gaps and feel less mechanical. Paged knobs exist since 0.7.0; it would be the Repeater's seventh knob
(page 2, knob G). To discuss: does the first, immediate hit always play (suggested) or is it subject to the chance
as well? Should accented steps have their own chance?

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).

### Full module descriptions on the main page

The one-line summary under the module menu is cut off for longer texts. Options to discuss:

- an **ⓘ button** next to the module menu that opens a small pop-up with the full description and a short
  explanation of every knob (suggested);
- a **hover help line** at the bottom of the editor that shows the full text of whatever the mouse is over
  (module summary, knob, button);
- a **tooltip** on the summary text (cheapest, but only appears after a delay);
- wrapping the summary onto two lines with a smaller font (limited room; the slot rows would get taller).

### Module browser with search

The module menu gets long as modules are added. Replace the drop-down with a **module browser** pop-up:
a search field at the top that has keyboard focus as soon as the browser opens, below it the modules grouped
by category, filtered as you type. The search matches the name, the category and the description words
(so "chord" also finds modules that only mention chords). Arrow keys and Enter pick a module, Esc closes.
The highlighted module's full description is shown at the bottom of the browser, so this also covers part of
the previous idea. To discuss: also recently used / favourite modules at the top?
