# MIDI Forge – Architecture

This document explains how the code is organised and why. It is kept true with every change
(see [CONTRIBUTING.md](../CONTRIBUTING.md)). API details live in the doc comments in the headers.

## Overview

```
 Host (Reaper, Bitwig …)
   │  MidiBuffer + playhead
   ▼
 Source/Plugin/PluginProcessor     JUCE AudioProcessor: parameters, state, presets
   │  MidiEventList + BlockContext + SlotSettings[8]
   ▼
 Source/Engine/ModuleChain         8 slots, note-consistency rules
   │  per slot
   ▼
 Source/Engine/Modules/*           the 14 modules (all derived from MidiModule)
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
| `Source/Plugin/PluginProcessor.*` | `MidiForgeProcessor`: host parameters, presets, state, MidiBuffer ↔ engine |
| `Source/Plugin/ParameterIds.h` | Host parameter IDs (stored in projects – never rename) |
| `Source/Plugin/FactoryPresets.*` | The factory presets (JUCE-free, so the engine tests can check them too) |
| `Source/UI/*` | Editor: `MidiForgeEditor`, `SlotRow`, `ParameterKnob`, `ForgeLookAndFeel`, `Theme` |
| `Tests/Engine/*` | Engine tests (no JUCE) |
| `Tests/Plugin/*` | Tests of the JUCE layer; can also render the editor to a PNG |
| `docs/` | This document, the user manual, images |
| `packaging/` | Files that go into the release zips |

## Parameters

Every slot has six host parameters: `sN_type` (module), `sN_on` (on/off) and `sN_a` … `sN_d`
(four generic values, normalised 0..1). A fixed parameter set is what hosts expect, so modules don't
add or remove parameters. Instead the **module description** (`ModuleDescriptions.cpp`) says what A … D
mean for each module: name, kind (integer, choice, note, percent, continuous), range, default and
display text. The host display, the editor and the modules all read from that one table.

**Compatibility rules** (projects and automation store these values):

- Never reorder or remove `ModuleType` entries or choice labels; only append.
- Never rename parameter IDs. Bump `ParameterIds::parameterVersion` when adding parameters.

## The processing path (audio thread)

1. `MidiForgeProcessor::processBlock` copies the slot parameters into `SlotSettings`, reads tempo and
   transport into a `BlockContext`, and converts the `juce::MidiBuffer` into a `MidiEventList`.
   SysEx and other long messages bypass the engine and are added back at the end.
2. `ModuleChain::process` feeds the events through the eight slots in order. A disabled or empty slot
   passes events through.
3. The output is converted back into the `juce::MidiBuffer`.

Everything on this path is **real-time safe**: no allocation, no locks, no I/O. All modules of all
types are created up front (every slot owns one instance of every module type), all buffers are
reserved in constructors, and queues have hard caps.

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
  (`BlockContext::blockStartSample`). Used by Arpeggiator, Note Echo and Humanize.

### Timing

- The chain counts samples since `prepare()`; scheduled events use that absolute time.
- The Arpeggiator follows the host's quarter-note position (`ppqPositionAtBlockStart`) while the
  transport runs, and its own clock otherwise. Gate lengths are capped one sample short of the step so
  a note always ends before the next starts.
- Note Echo computes its delay from the tempo at note-on.

## Randomness

Modules use `std::minstd_rand` seeded through `setRandomSeedForNewModules()`. The plug-in seeds from the
clock; tests use fixed seeds so failures are reproducible.

## Build and CI

See [CONTRIBUTING.md](../CONTRIBUTING.md#building) for commands. The workflow
`.github/workflows/build.yml` runs:

1. **Checks** (Linux): clang-format, clang-tidy, engine tests with warnings as errors, engine tests
   under address/UB sanitizers, plug-in layer tests, and an editor screenshot (artifact `editor-screenshot`).
2. **Plug-in builds** (Windows, macOS universal): VST3 + CLAP, zipped as artifacts. macOS builds are
   ad-hoc signed.
3. **Release** (tags `v*` only): attaches the zips to a GitHub Release.

Dependencies (JUCE 9.0.4, clap-juce-extensions) are fetched by CMake at pinned versions; GitHub
Actions are pinned to commit SHAs and updated by Dependabot.

## Licensing note

JUCE 9 is dual licensed (AGPLv3 or a commercial JUCE licence, which has a free tier for small
developers). Distributing MIDI Forge binaries requires complying with one of them. The repository
does not yet contain a licence file; choosing one is the owner's decision.
