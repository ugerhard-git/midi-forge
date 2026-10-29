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
connects it to JUCE, which provides the VST3 wrapper, parameters and the editor. A CLAP wrapper from
`clap-juce-extensions` can be switched on with `-DMIDI_FORGE_BUILD_CLAP=ON` (off for now).

## Source layout

| Path | Contents |
|------|----------|
| `Source/Engine/MidiEvent.h` | `MidiEvent` (one short MIDI message), `MidiEventList`, `BlockContext` (tempo, transport, time signature, bar start, block timing; `barPositionAtBeat()`, `barsAtBeat()`) |
| `Source/Engine/Meter.h` | `TimeSignature` (from the host, 4/4 fallback) and `BarStructure`: groups of two and three counts (`OddMeterGrouping` for 5/8, 7/8 …), accent levels of a position, backbeats |
| `Source/Engine/ModuleDescriptions.*` | The table of all module types and their parameters; value conversion, formatting and parsing; scales and `moveByScaleSteps()` (Mutate, Diatonic Harmonizer), chords, note divisions (`divisionLengthInBeats()`, `divisionsBetween()` for bar-long divisions) |
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
| `Source/UI/*` | Editor: `MidiForgeEditor` (tabs; the header's word mark, glow and ember line come from `ForgeLogo`, drawn with paths and gradients), `SlotRow` + `ParameterKnob` (Chain tab), `ModuleBrowser` + `ModuleInfoView` (pop-ups of the module menu and the ⓘ button), `ModulationPage` with `LfoPanel`, `WaveformPreview`, `NoteRandomPanel`, `RouteRow`, `CompactKnob` (Modulation tab), `PresetBar` (◀ menu ▶, Save, ⋯ menu with async dialogs), `ForgeLookAndFeel`, `Theme`. `ParameterKnob::attachTo()` sets the knob's step interval from the parameter description, so stepped parameters snap while dragging |
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

The same idea keeps the synth in tune: `ModuleChain` tracks the last pitch bend per channel at every slot's
input and output (`PitchBendTracker`). When a slot restarts and its output bend differs from its input
bend (a Pitch Slide in the middle of a slide), it sends the input's bend. Panic sends a centred pitch bend
on all channels. The stress test also checks that no channel is left bent.

## Module families

- **`NoteMappingModule`** – decides everything at note-on time. Subclasses implement `createNoteOns()`
  and optionally `transformOtherEvent()`. Used by filters, transforms, Chord, Probability, Random Pitch.
- **`SchedulingModule`** – sends events in the future using a queue of absolute sample times
  (`BlockContext::blockStartSample`). Used by Arpeggiator, Repeater, Note Echo and Humanize.
- **Groove** is a `SchedulingModule` (like Humanize): each note-on is delayed by Early plus the groove shift
  of its nearest grid step (clamped at 0), its note-off by the same amount. Grid steps count from the bar
  start (`BlockContext::barPositionAtBeat()`); Laid-back Snare asks `BarStructure::isBackbeat()`. The Early time is reported as latency (see
  `reportLatencyOfModules()` below), so hosts compensate.
- **Strum** (a `SchedulingModule`) collects note-ons arriving within Window, sorts them, and schedules them
  Speed apart starting Window after the first one; everything else is delayed by Window. Each key stores its
  delay for its note-off. A key already waiting in a chord is not collected twice.
- **Ghost Notes** (a `SchedulingModule`) passes every event on time and schedules ghost note pairs (1/64 long)
  after a note, and before the places where notes fell one bar earlier (a list of the last bar's note-ons
  with their bar length, while the host plays). A key repeated before its note-off ends the earlier note first, so the one note-off
  upstream sends ends the right note.
- `MidiForgeProcessor::reportLatencyOfModules()` adds up Groove's Early and Strum's Window of every
  switched-on slot and reports it with `setLatencySamples()`.
- **Note Dispatcher** derives from `MidiModule` directly. Every input key remembers what happened to its note
  (`channelOfInputKey`: the channel it was sent to, *passed through* when it was not on the Input channel,
  or *stolen* when its channel was freed for a newer note), so note-offs and poly pressure always follow
  their note, whatever the knobs say by then. A key that sounds again ends its previous note first, as a
  synth would. Free channels are counted per channel (`numNotesOnChannel`); the oldest is found by the order
  in which channels last got a note or chord.
- **Mutate** derives from `MidiModule` directly. It keeps offsets in scale steps per loop position (4 bars of
  up to 64 sixteenths, counted from the bar start; cleared when the time signature changes) or per pitch, changes them by a Return chance (one step home) and an Amount chance (1..Range
  steps away, at most 7) whenever a single note plays, and maps each key to the pitch it started (for its
  note-off). Chord notes (onsets within 30 ms) pass; a changed first note of a chord is restarted at its own
  pitch. Its Reset is a `ParameterKind::Trigger` parameter: the module acts on the rising edge (also when
  modulated); the editor shows trigger parameters as a button that is on while held.
- **Beat Thinning** is a `NoteMappingModule`; its `process()` keeps a pointer to the block context for
  `createNoteOns()`. A note's nearest grid step gets an accent level from `BarStructure`; the drop chance is
  Amount × level / finest level of the grid (bar start 0, the finest level 1). Notes within 30 ms of the
  previous note share its decision.
- **Diatonic Harmonizer** derives from `MidiModule` directly and keeps a `NoteMappingTable` and a
  `SoundingNoteCounter` itself: a note-on starts the note plus its harmony notes (`moveByScaleSteps()`, octaves
  ±12). A note-on within 30 ms of the previous one is a chord note: it plays alone and ends the harmony notes
  of the previous note (entries 1.. of its mapping).
- **Pitch Slide** derives from `MidiModule` directly: notes pass through on time, and the module adds pitch
  bend messages per channel (about one per millisecond while a slide moves, only when the value changes).
  It remembers the player's bend and adds the slide to it; when a channel's last held key ends, it sends the
  player's bend again. Notes within 30 ms of a slide's first note share its slide (one Chance roll).
  The fall modes (0.21.0) hold note-offs back (`releaseTimeOfKey`): when a channel's last key is released, a
  fall from the current slide position to Offset starts and every held-back note ends at its end
  (`endFall()`); keys released while others are held end 30 ms later unless the chord follows
  (`endDueReleasedNotes()`); a note-on cuts a fall short. `isParameterInactive()` (ModuleDescriptions) tells
  the editor to dim Delay in these modes.

New module types are appended to `ModuleType` (their numbers are stored in projects); the module browser
groups modules by category, so a new Generator still appears under *Generators*.

### Knobs added to existing modules

A module may gain knobs in a later version (Repeater: E and F in 0.7.0, G in 0.9.0, H in 0.19.1; Arpeggiator:
E in 0.19.1). Old projects and presets
hold 0 for such a knob (its value while unused), which can be a bad setting (Chance 0 %). So every
`ParameterDescription` names the knob layout version that added it (`addedInKnobLayout`), states store
`knobLayout` (= `currentKnobLayoutVersion`), and after loading a project or user preset
`MidiForgeProcessor::resetKnobsAddedAfter()` sets newer knobs of the loaded modules to their defaults.
States without the property are from 0.7.0 (they have `shownPages` / `s1_e`) or older. When adding a knob to
an existing module: mark it with `addedInLayout (n)` and raise `currentKnobLayoutVersion` to n.

## Moving and removing modules

`MidiForgeProcessor::swapSlots()` and `removeModule()` copy whole slots (`SlotContents`: module type,
on/off, the eight knob values, the shown page) as host parameter gestures, then `moveRouteTargets()` points
every route at the slot its module moved to (routes to a removed module get target 0). `removeModule()`
keeps the removed slot's contents and the old targets of its routes (`RemovedModule`), so
`undoModuleRemoval()` can move the modules back down and restore them exactly. Undo is possible while the
last slot is still empty. The editor notices a removal through `getModuleRemovalCount()` and shows the undo
strip in the footer for 8 seconds. Removal history is not saved in projects.

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
  while the transport runs, and their own clock otherwise. `StepGrid` (GeneratorModules.cpp) maps step
  numbers to host positions: steadily from the song start, or (Grid *Bar*) restarting at every bar start,
  with bar b holding steps b × stepsPerBar …; the last step of a bar may be shorter. Swing parity and
  accents use the step number within the bar (`stepInBar()`). The Repeater numbers its grid steps (swing
  delays odd steps, accents are chosen by step number) and re-synchronises after transport jumps or a
  switch between host and own clock, so steps are never replayed or fired in a burst. Gate lengths are capped one sample short of the step so
  a note always ends before the next starts.
- Note Echo computes its delay from the tempo at note-on.
- **Time signature:** `readBlockContext()` copies the host's time signature (`TimeSignature::fromHost()`, 4/4
  when missing or nonsensical) and the start of the current bar (`getPpqPositionOfLastBarStart()`) into the
  `BlockContext`. `barPositionAtBeat()` turns a position into bar number, position in the bar and bar length
  (bars counted from that bar start; without one, from the song start). Bar-long divisions carry their
  number of bars (`NoteDivision::bars`); LFOs and step modulators count them with `divisionsBetween()`, so
  they stay on the bar grid in any meter. Owner's rule 11: no module may assume 4/4.

## Modulation

- `StepModulator` (Source/Engine/Modulation) plays 4 … 16 step values. Its clock works like a tempo-synced
  LFO: the step position is the host position divided by the step length while the transport runs (so
  step 1 sits on the bar grid), otherwise its own clock; "restart on key" re-anchors it. Glide slides to
  the next value over the last part of a step (smoothstep). Tied steps (`isTied`, 0.16.0, host parameters
  `qn_t2..16`, version 6) play on the value of their group's first step; the group counts as one long step
  for the glide. Sources 8..11 are Steps 1..4 (appended after
  LFO 1..4 and Random 1..4). Host parameters `qn_steps`, `qn_rate`, `qn_glide`, `qn_restart`, `qn_v1..16`
  (version 5) come after all older parameters.
- The editor's Modulation tab has sub-tabs (`ModulationPage::SourcePage`: LFOs, Random, Steps); each gets the
  whole sources area, the routes stay below. `StepModulatorPanel` and `StepEditor` draw and edit the steps;
  each drawn value is one host parameter gesture.

- Routes can also target a slot's on/off switch (targets 65..72, `ModulationRoute::targetsSlotSwitch()`,
  parameter index `slotSwitchParameterIndex`). `applyModulationRoutes()` adds the switch routes to 0 or 1 and
  applies hysteresis (on above `switchOnThreshold`, off below `switchOffThreshold`), remembering each slot's
  state in `MidiForgeEngine::slotSwitchStates`. The chain sees an ordinary switch change, so the usual slot
  restart rules (release, orphaned keys) apply.

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
- **Modulating the modulation (0.20.0):** 12 routes (`rn_*` for n = 9..12 are host parameters of version 7,
  appended after everything else, polarity inside their group). Targets 73..84 are the route amounts,
  85..96 LFO Rate/Phase/Width, 97..104 Steps Rate/Glide (`routeAmountTarget()`, `lfoSettingTarget()`,
  `stepSettingTarget()`). At the start of each segment `MidiForgeEngine::updateModulatedModulators()` calls
  `applyRoutesToModulators()` (Modulation/ModulatorModulation.*) with the **previous** segment's source values
  and route amounts, giving `ModulatedModulators` (LFO and step settings, route amounts) for this segment;
  the sources are then evaluated and advanced with these settings, and the slot routes use the modulated
  amounts. The one-segment delay keeps feedback loops stable. Settings move in normalised control space
  (LFO rate via `normalisedLfoRate()` / `setLfoRateFromNormalised()`, mirroring the host ranges); route amounts
  add directly (−1..+1), the phase wraps.
- **Smooth rate changes:** while a route moves its rate, an `Lfo` / `StepModulator` sets `isOffGrid`
  (`followRateModulation()`) and runs on its own clock, which until then mirrored the host position, so the
  speed changes without a jump. `restart()` and the transport starting (`lockToGrid()`, from
  `MidiForgeEngine::processSegment()`) put it back on the bar grid.
- The processor publishes the modulated settings (`modulatedSettingReadouts`, in host parameter units);
  `getModulatedSettingText()` formats them with the host parameter's own text function, and the
  Modulation page shows them in the modulation colour (`CompactKnob::showModulatedValue()`, route amount
  labels, the step modulator's glide and rate menu).
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
2. **Plug-in build** (Windows): the VST3, INSTALL.txt and LICENSE.txt as artifact `MIDI-Forge-Windows`,
   uploaded as a folder (GitHub zips artifacts itself, so the download is a single zip). macOS and CLAP
   builds are switched off for now (see docs/IDEAS.md); CMake still knows both (universal binary on macOS,
   `MIDI_FORGE_BUILD_CLAP`).
3. **CodeQL (C++)**: security analysis (`security-extended` queries) of MIDI Forge's own code; results
   appear under the repository's *Security → Code scanning*. CodeQL analyses every file compiled while it
   watches the build, and its path filters do not apply to C++, so the job first builds everything without
   CodeQL, then deletes only our own object files (`Source/`, `Tests/`) and rebuilds them under CodeQL. JUCE
   thus stays out of the results. (Code scanning's *default setup* must stay off: it conflicts with this
   workflow.)
4. **Release** (tags `v*` only, after all of the above): zips each artifact folder, takes the text
   from the version's section of `CHANGELOG.md` (no section: the job fails) and creates the GitHub release –
   a pre-release for versions below 1.0 or tags with a suffix. If a release for the tag exists already, it
   only uploads the zips (`--clobber`).

**JUCE modules linked** (plug-in and plug-in tests): `juce_audio_processors` – the plug-in wrapper (VST3),
host parameters and state; it brings `juce_audio_processors_headless`, `juce_audio_basics`, `juce_events`,
`juce_gui_extra`, `juce_data_structures` and `juce_core` along – and `juce_gui_basics` for the editor.
`juce_audio_utils` is deliberately **not** linked: it pulls in `juce_audio_formats` (audio file codecs such as
MP3, Ogg Vorbis, FLAC, Opus) and `juce_audio_devices` (audio and MIDI drivers), which a MIDI effect never
uses. Leaving it out means less third-party code compiled, scanned and trusted.

Dependencies (JUCE 9.0.4; clap-juce-extensions only with CLAP switched on) are fetched by CMake at pinned versions; GitHub
Actions are pinned to commit SHAs and updated by Dependabot.

## Licensing note

MIDI Forge is licensed under the **GNU Affero General Public License v3** (`LICENSE`). JUCE 9 is dual
licensed (AGPLv3 or a commercial JUCE licence); MIDI Forge uses it under the AGPLv3, so the plug-in as a whole
is AGPLv3. The VST3 SDK that JUCE ships is MIT licensed since VST 3.8. Every release zip contains the licence
(`LICENSE.txt`) and `INSTALL.txt`, which names the licence, the missing warranty and where the source is. The
editor shows the same notices under ⋯ → About MIDI Forge (`getAboutText()` in `PresetBar`), as the AGPLv3
asks of interactive programs.
Contributions are accepted under the same licence. Dependencies are pinned: JUCE by tag plus a check of the
fetched commit in `CMakeLists.txt`, CLAP extensions by commit.
