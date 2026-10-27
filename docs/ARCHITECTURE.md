# MIDI Forge – Architecture

This document explains how the code is organised and why. It is kept true with every change
(see [CONTRIBUTING.md](../CONTRIBUTING.md)). API details live in the doc comments in the headers.

## Overview

```
 Host (Reaper, Bitwig …)
   │  MidiBuffer + playhead
   ▼
 Source/Plugin/PluginProcessor     JUCE AudioProcessor: parameters, state, presets
   │  MidiEventList + BlockContext + EngineSettings (slots, LFOs, routes)
   ▼
 Source/Engine/MidiForgeEngine     LFOs + routes; splits the block into segments
   │  per segment: modulated SlotSettings[8]
   ▼
 Source/Engine/ModuleChain         8 slots, note-consistency rules
   │  per slot
   ▼
 Source/Engine/Modules/*           the 16 modules (all derived from MidiModule)
```

The **engine** (`Source/Engine`) is plain C++17 without JUCE. It can be built and tested on any
machine in seconds (`midi_forge_engine_tests`). The **plug-in layer** (`Source/Plugin`, `Source/UI`)
connects it to JUCE, which provides the VST3 wrapper, parameters and the editor; the CLAP wrapper comes
from `clap-juce-extensions`.

## Source layout

| Path | Contents |
|------|----------|
| `Source/Engine/MidiEvent.h` | `MidiEvent` (one short MIDI message), `MidiEventList`, `BlockContext` (tempo, transport, block timing) |
| `Source/Engine/ModuleDescriptions.*` | The table of all module types and their parameters; value conversion, formatting and parsing; scales, chords, note divisions |
| `Source/Engine/MidiModule.*` | Base classes `MidiModule`, `NoteMappingModule`, `SchedulingModule`, helpers `SoundingNoteCounter`, `NoteMappingTable`; module factory |
| `Source/Engine/Modules/*` | The modules, one file per category |
| `Source/Engine/ModuleChain.*` | The eight-slot chain |
| `Source/Engine/MidiForgeEngine.*` | The complete engine: LFOs, modulation routes and the chain |
| `Source/Engine/Modulation/Lfo.*` | `Lfo`: waveforms, tempo sync, phase, restart; LFO division table |
| `Source/Engine/Modulation/NoteRandom.*` | `NoteRandom`: a random value per played note, chord window, phrase mode, steps |
| `Source/Engine/Modulation/ModulationRoutes.h` | `ModulationRoute` (source, target, amount, polarity), source and target numbering, `applyModulationRoutes()` |
| `Source/Plugin/PluginProcessor.*` | `MidiForgeProcessor`: host parameters, presets, state, MidiBuffer ↔ engine |
| `Source/Plugin/ParameterIds.h` | Host parameter IDs (stored in projects – never rename) |
| `Source/Plugin/FactoryPresets.*` | The factory presets (JUCE-free, so the engine tests can check them too) |
| `Source/Plugin/UserPresetLibrary.*` | User preset files: folder, save/load/rename/delete/import, validation, name cleaning |
| `Source/Plugin/RecentModuleList.*` | The recently used modules of the module browser, shared by all instances via `juce::SharedResourcePointer`, saved in `Documents/MIDI Forge/Settings.xml` |
| `Source/UI/*` | Editor: `MidiForgeEditor` (tabs), `SlotRow` + `ParameterKnob` (Chain tab), `ModuleBrowser` + `ModuleInfoView` (pop-ups of the module menu and the ⓘ button), `ModulationPage` with `LfoPanel`, `WaveformPreview`, `NoteRandomPanel`, `RouteRow`, `CompactKnob` (Modulation tab), `PresetBar` (◀ menu ▶, Save, ⋯ menu with async dialogs), `ForgeLookAndFeel`, `Theme`. `ParameterKnob::attachTo()` sets the knob's step interval from the parameter description, so stepped parameters snap while dragging |
| `Tests/Engine/*` | Engine tests (no JUCE) |
| `Tests/Plugin/*` | Tests of the JUCE layer; can also render both editor pages and the module pop-ups to PNGs |
| `docs/` | This document, the user manual, images |
| `packaging/` | Files that go into the release zips |

## Parameters

Every slot has ten host parameters: `sN_type` (module), `sN_on` (on/off) and `sN_a` … `sN_h`
(eight generic values, normalised 0..1; `sN_e` … `sN_h` were added in 0.7.0 and come after all older
parameters). The editor shows them in pages of four (`numParametersPerPage`); modules with more than four
used parameters get page arrows. The page shown per slot is not a host parameter: the processor keeps it
(`getShownPage`/`setShownPage`) and saves it as the property `shownPages` of the state tree in projects and
user presets; loading a module resets it to page 1. A fixed parameter set is what hosts expect, so modules don't
add or remove parameters. Instead the **module description** (`ModuleDescriptions.cpp`) says what A … D
mean for each module: name, kind (integer, choice, note, percent, continuous), range, default and
display text. The host display, the editor and the modules all read from that one table.

LFOs and routes have their own host parameters (`lN_wave`, `lN_sync`, `lN_rate`, `lN_div`, `lN_phase`,
`lN_width`, `lN_restart`, `rN_source`, `rN_target`, `rN_amount`), appended after the slot parameters.
Version 0.6.0 appended the Note Random parameters (`nN_window`, `nN_trigger`, `nN_steps`) and the route
polarities (`rN_polarity`, in a group "Route Polarity" at the very end so older parameters keep their
positions). A route's source is a number: 0–3 = LFO 1–4, 4–7 = Random 1–4 (appended to the source
choices, so stored projects keep their sources). A route's target is a number: 0 = none, then slot 1 A …
slot 8 D (`modulationTargetIndex()`).

**Compatibility rules** (projects and automation store these values):

- Never reorder or remove `ModuleType` entries or choice labels; only append.
- Never reorder `LfoWaveform` values or the `lfoDivisions` table; only append.
- Never rename parameter IDs. New parameters get a new version number in `ParameterIds`
  (slot parameters: 1, modulation parameters: 2, Note Random and polarity: 3, slot parameters E–H: 4).
- Modulation target numbers: A–D of every slot keep 1 … 32; E–H are 33 … 64
  (`firstSecondPageTarget`). The route target menu is ordered by slot and lists only knobs the loaded
  modules use, so it has its own attachment (`juce::ParameterAttachment`) instead of a ComboBoxAttachment.
- Factory presets give values for A–D in `PresetSlot::plainValues`; page 2 (`secondPageValues`) defaults
  to "keep the module's default", so presets written before 0.7.0 need no change.

## The processing path (audio thread)

1. `MidiForgeProcessor::processBlock` copies the slot parameters into `SlotSettings`, reads tempo and
   transport into a `BlockContext`, and converts the `juce::MidiBuffer` into a `MidiEventList`.
   SysEx and other long messages bypass the engine and are added back at the end.
2. `MidiForgeEngine::process` runs the LFOs and the chain (see [Modulation](#modulation)).
   `ModuleChain::process` feeds the events through the eight slots in order. A disabled or empty slot
   passes events through.
3. The output is converted back into the `juce::MidiBuffer`.

Everything on this path is **real-time safe**: no allocation, no locks, no I/O. All modules of all
types are created up front (every slot owns one instance of every module type), all buffers are
reserved in constructors, and queues have hard caps.

## Note Split: the bypass path

`MidiModule::processWithBypass()` lets a module send events past the remaining slots; only Note Split
uses it (all other modules inherit the default, which calls `process()`). `ModuleChain` collects these
events per block, tracks the notes each slot sent past the chain (`notesBypassedBySlot`) and merges them
into the chain's output at the end (sorted by position). When the slot restarts (other module, switched
off) it releases those notes on the bypass path too; panic clears everything as before.

Note Split decides the path at note-on and remembers it per key, so note-offs and poly aftertouch follow
their note even if the range changes. Bypassed notes pass exactly as played (including repeated note-ons
of a held key), so their note-offs are not counted. Non-note messages always continue down the chain.

## No stuck notes: the rules

Stuck notes are the classic bug of MIDI effects. Three mechanisms prevent them; the randomized stress
test in `Tests/Engine/ModuleChainTests.cpp` checks them across hundreds of random chains.

1. **Note mapping** (`NoteMappingModule`): when a note-on comes in, the module remembers which notes it
   sent for it (`NoteMappingTable`). The matching note-off releases exactly those, even if parameters
   changed in between.
2. **Sounding-note counting** (`SoundingNoteCounter`): if two inputs produce the same output note, the
   note-off is only sent when the last one ends. Unknown note-offs always pass through.
3. **Slot restarts** (`ModuleChain`): when a slot changes module or is switched on/off, the chain sends
   note-offs for everything that slot was outputting (they travel through the downstream slots, which
   release what they made from them) and marks the notes held at the slot's input as *orphaned*. Their
   note-offs, arriving later, are swallowed. Panic does the same for every slot.

## Module families

- **`NoteMappingModule`** – decides everything at note-on time. Subclasses implement `createNoteOns()`
  and optionally `transformOtherEvent()`. Used by filters, transforms, Chord, Probability, Random Pitch.
- **`SchedulingModule`** – sends events in the future using a queue of absolute sample times
  (`BlockContext::blockStartSample`). Used by Arpeggiator, Repeater, Note Echo and Humanize.

New module types are appended to `ModuleType` (their numbers are stored in projects); the module browser
groups modules by category, so a new Generator still appears under *Generators*.

### Knobs added to existing modules

A module may gain knobs in a later version (Repeater: E and F in 0.7.0, G in 0.9.0). Old projects and presets
hold 0 for such a knob (its value while unused), which can be a bad setting (Chance 0 %). So every
`ParameterDescription` names the knob layout version that added it (`addedInKnobLayout`), states store
`knobLayout` (= `currentKnobLayoutVersion`), and after loading a project or user preset
`MidiForgeProcessor::resetKnobsAddedAfter()` sets newer knobs of the loaded modules to their defaults.
States without the property are from 0.7.0 (they have `shownPages` / `s1_e`) or older. When adding a knob to
an existing module: mark it with `addedInLayout (n)` and raise `currentKnobLayoutVersion` to n.

## Module browser and help texts

- Every `ModuleDescription` carries a one-line `summary` (shown under the module menu), `details` (a few
  sentences) and, per parameter, a one-line `helpText` (added with `ParameterDescription::withHelp()`).
  `ModuleInfoView` shows details and help texts in the ⓘ pop-up and inside the browser; the knobs use the
  help text as tooltip. An engine test makes sure no module or used knob lacks its text.
- The module menu is a `ModuleMenu` (a `juce::ComboBox` whose `showPopup()` opens the `ModuleBrowser` in a
  `juce::CallOutBox` inside the editor, so keyboard focus stays in the plug-in window). The combo box items
  only provide the text of the closed menu.
- Search: `getSearchRelevance()` (engine, JUCE-free) splits the text into words that must all appear,
  ignoring case; it rates a match by where it was found (name > category/summary > details/knob names).
  The browser lists every match by category and highlights the first best-rated one.
- `RecentModuleList` keeps the last five modules picked in the browser (not those loaded by presets or
  automation), newest first. The file `Documents/MIDI Forge/Settings.xml` (`<MidiForgeSettings>`) is read
  defensively (64 KB limit, own root tag, unknown or repeated module numbers skipped); saving keeps other
  elements in the file and re-reads it first, so several plug-in instances don't overwrite each other.

### Timing

- The chain counts samples since `prepare()`; scheduled events use that absolute time.
- The Arpeggiator and the Repeater follow the host's quarter-note position (`ppqPositionAtBlockStart`)
  while the transport runs, and their own clock otherwise. The Repeater numbers its grid steps (swing
  delays odd steps, accents are chosen by step number) and re-synchronises after transport jumps or a
  switch between host and own clock, so steps are never replayed or fired in a burst. Gate lengths are capped one sample short of the step so
  a note always ends before the next starts.
- Note Echo computes its delay from the tempo at note-on.

## Modulation

- `Lfo` computes its output from a position measured in cycles. Tempo-synced LFOs take it from the
  host's quarter-note position while the transport runs (locked to the bar grid; "restart on key"
  remembers the host position of the restart), otherwise from their own clock, which mirrors the host
  position while following it so stopping the transport causes no jump.
- The random waves derive their value from a hash of (seed, cycle number), so they need no state and
  give the same values when the host jumps back to the same position.
- **Sources:** `MidiForgeEngine` computes every source in both polarities per segment
  (`ModulationSourceValues`): bipolar −1 … +1 and unipolar 0 … +1. For LFOs the unipolar value reads Sine
  and Triangle a quarter cycle earlier (`Lfo::unipolarValueAt()`), so their cycle starts at the lowest point.
  Note Random values map as (v + 1) / 2.
- **Note Random:** `MidiForgeEngine::handleNotesForModulators()` tells each `NoteRandom` about every note-on
  played into the plug-in (with the absolute sample time and whether it is the first key of a phrase).
  A new value is rolled unless the note lies within the chord window after the last roll (or, in phrase
  mode, other keys are held). Because blocks are already split at every input event, the new value
  applies from that very note on. Values come from a SplitMix64 generator seeded per modulator.
- `applyModulationRoutes()` adds `sourceValue × amount` (bipolar or unipolar value, per route) to each
  target's normalised value (0..1), sums routes on the same target and clamps. The user's own settings are
  never changed.
- **Segments:** with at least one active route, `MidiForgeEngine` splits each block at every input event
  and at least every 32 samples (`maxSegmentLength`), evaluates the LFOs at the start of each segment and
  runs the chain on each segment with its own `BlockContext` (shifted host position, segment length).
  Without active routes the block is processed in one piece, exactly as before.
- Modules already handle parameter changes mid-note (rule 1 below), so modulation cannot cause stuck
  notes; the stress test runs half of its random chains with random LFO routings.
- The engine publishes the modulated slot values and LFO outputs/phases after each block; the processor
  copies them into atomics that the editor reads for the rings and previews.

## Presets

- **Factory presets** (`FactoryPresets.cpp`) are the host's "programs". Loading one sets every slot, LFO and
  route parameter as a user gesture, so hosts can record it.
- **User presets** are files managed by `UserPresetLibrary` in `Documents/MIDI Forge/Presets`. Format:
  `<MidiForgePreset formatVersion="1">` containing the same `<MidiForge>` parameter tree that DAW projects
  store. Saving writes to a temporary file first and then replaces the target, so a crash never leaves a
  half-written preset.
- **Validation:** files over 256 KB, unparsable XML, a wrong root tag or a missing `<MidiForge>` child are
  refused. `MidiForgeProcessor::applyStateValues()` applies a state parameter by parameter: values are
  clamped to the range, non-finite values and missing parameters fall back to defaults, unknown entries
  are ignored. Names are cleaned (`UserPresetLibrary::cleanName()`): no path separators or other
  characters Windows/macOS forbid, no control characters, no leading/trailing dots or spaces, no reserved
  Windows device names, at most 64 characters – so a name can never point outside the preset folder.
- The processor tracks the current preset (`PresetKind`: none, factory, user) and a snapshot of all
  parameter values taken when it was loaded or saved; `isCurrentPresetModified()` compares against it
  (the `*` in the menu). Projects store the kind and name, so reopening shows the right preset.
- All preset handling runs on the message thread; the audio thread only ever sees parameter values.
- Dialogs are asynchronous (`AlertWindow::enterModalState`, `FileChooser::launchAsync`) because plug-ins
  must not block the host; their callbacks hold weak references to the `PresetBar`.

## Randomness

Modules use `std::minstd_rand`, and LFOs a hash, both seeded through `setRandomSeedForNewModules()` /
`takeNextRandomSeed()`. The plug-in seeds from the clock; tests use fixed seeds so failures are reproducible.

## Build and CI

See [CONTRIBUTING.md](../CONTRIBUTING.md#building) for commands. The workflow
`.github/workflows/build.yml` runs:

1. **Checks** (Linux): clang-format, clang-tidy, engine tests with warnings as errors, engine tests
   under address/UB sanitizers, plug-in layer tests, and screenshots of both editor pages (artifact
   `editor-screenshots`, the source of `docs/images/editor.png` and `docs/images/modulation.png`).
2. **Plug-in builds** (Windows, macOS universal): VST3 + CLAP, zipped as artifacts. macOS builds are
   ad-hoc signed.
3. **Release** (tags `v*` only): attaches the zips to a GitHub Release.

Dependencies (JUCE 9.0.4, clap-juce-extensions) are fetched by CMake at pinned versions; GitHub
Actions are pinned to commit SHAs and updated by Dependabot.

## Licensing note

JUCE 9 is dual licensed (AGPLv3 or a commercial JUCE licence, which has a free tier for small
developers). Distributing MIDI Forge binaries requires complying with one of them. The repository
does not yet contain a licence file; choosing one is the owner's decision.
