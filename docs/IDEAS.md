# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### Module browser with search and ⓘ module info

Agreed with the owner, waiting for the go.

- **ⓘ button** next to each slot's module menu. It opens a pop-up with the module's full description and one line
  per knob explaining what it does (both pages). Empty slots have no ⓘ.
- **Module browser** instead of the drop-down: a search field at the top with keyboard focus as soon as it opens,
  then the modules grouped by category, filtered as you type. The search matches the name, the category and
  words in the description. Arrow keys and Enter pick a module, Esc closes. The bottom of the browser shows the
  highlighted module's full description (the same panel as the ⓘ pop-up).
- **Recently used**: the last 5 chosen modules at the top of the browser, hidden while searching. Shared by all
  plug-in instances and projects, stored in a small settings file next to the user presets. No favourites.
- Each knob gets a one-line help text in the module descriptions (engine), shown in the ⓘ pop-up, the browser and
  as the knob's tooltip. The manual gets the same per-knob explanations.

## Ideas

### Repeater: Chance knob

A *Chance* knob for the Repeater: each repeat plays only with this probability (0 … 100 %), so ratchets get
gaps and feel less mechanical. Paged knobs exist since 0.7.0; it would be the Repeater's seventh knob
(page 2, knob G). To discuss: does the first, immediate hit always play (suggested) or is it subject to the chance
as well? Should accented steps have their own chance?

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).
