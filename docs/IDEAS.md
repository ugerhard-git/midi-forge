# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### User presets

Save and name presets inside the plug-in. Design agreed; waiting for the go.

- **Save / Save As / Rename / Delete** next to the preset menu; saving asks for a name. A preset holds the
  complete setup: all slots, LFOs and routes.
- Stored as one XML file per preset (`Name.mfpreset`) in `Documents/MIDI Forge/Presets/` (Windows and macOS),
  so presets work across projects and DAWs and can be backed up or shared. **Flat list**, no subfolders.
- Preset menu with *Factory* and *User* (alphabetical) sections; **◀ ▶** buttons to step through presets;
  **`*`** after the name when the current settings differ from the loaded preset.
- **Show folder** (opens Explorer/Finder) and **Import…** (copies a preset file into the folder).
- The DAW's program list keeps showing only the factory presets (hosts handle changing program lists badly).
- Files are validated like project state (size limit, own format only, values clamped); names are
  sanitised to valid file names.
- Tests: save/load round trip, rename, delete, import, damaged or foreign files ignored, name sanitising.

## Ideas

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
The LFOs exist now (0.2.0); needs a design discussion (which targets, how the menu lists them).
