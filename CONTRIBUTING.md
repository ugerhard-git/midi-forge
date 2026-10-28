# Contributing to MIDI Forge

These rules apply to every change, by people and by AI assistants alike. AI-specific working rules are in
[CLAUDE.md](CLAUDE.md).

## Ground rules

1. **Readable code.** Use understandable, generous names for variables, functions, classes and files:
   `noteNumber`, `samplesPerBeat`, `releaseOutputNotes()` – not `n`, `spb`, `rel()`. One- and two-letter
   names are only acceptable for the conventional `x`, `y` and `g` (graphics). clang-tidy enforces a
   minimum name length.
2. **Explain the code.** Comment what a piece of code does and why, in a form IDEs pick up: Doxygen
   comments (`/** ... */`, `///<`) on every class, function and non-obvious member, `@param` / `@return`
   where they help. Use ordinary `//` comments inside functions for the *why* of non-obvious steps.
3. **Documentation stays true.** Every change that affects architecture, build, behaviour or file
   layout updates [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and the doc comments in the same commit.
4. **The manual stays true.** Every user-visible change (modules, parameters, ranges, presets, UI,
   installation) updates [docs/MANUAL.md](docs/MANUAL.md) in the same commit. If the editor looks
   different, update `docs/images/editor.png` and `docs/images/modulation.png` from the CI artifact
   `editor-screenshots`.
5. **Tests.** Write tests whenever possible or sensible, and keep them up to date with the code:
   - engine behaviour → `Tests/Engine/` (fast, no JUCE),
   - plug-in layer (parameters, presets, state, editor) → `Tests/Plugin/`,
   - every bug fix gets a test that would have caught it.
6. **Formatting and linting.** Code is formatted with clang-format (`.clang-format`) and checked with
   clang-tidy (`.clang-tidy`) and compiler warnings as errors. CI rejects changes that fail them.
7. **Security.** Treat everything from outside as untrusted: project state from the host, parameter
   text typed by users, MIDI from any source. Validate and clamp, never trust sizes or indices.
   Keep dependencies pinned (exact JUCE tag plus its commit in `MIDI_FORGE_JUCE_COMMIT`, commit SHAs for other repositories and for GitHub Actions)
   and workflow permissions minimal. Never commit secrets.
8. **Performance.** Code on the audio thread (`processBlock`, `ModuleChain::process`, every
   `MidiModule::process`) must be real-time safe: no memory allocation, no locks, no file or network
   access, no unbounded loops. Reserve memory in constructors and cap queue sizes.
9. **Compatibility.** Parameter IDs, `ModuleType` values and choice label order are stored in user
   projects: never rename, reorder or remove them – only append. A knob added to an existing module is
   marked with `addedInLayout (n)` and raises `currentKnobLayoutVersion`, so old projects get its default
   (see docs/ARCHITECTURE.md, "Knobs added to existing modules").
10. **New features are discussed first.** When the owner asks about a possible feature, discuss and plan
    it, then wait for an explicit go before writing code or building. Discussed features that are not
    built yet are recorded in [docs/IDEAS.md](docs/IDEAS.md).
11. **No assumptions about the time signature.** Bar lengths, beats and accents come from the host's time
    signature and bar start, never from a hard-coded 4/4. When a planned feature depends on the meter, the
    plan says so explicitly and lists options for handling it, so the owner can decide.

## Building

Nothing has to be built locally: every push builds the plug-in on GitHub Actions (see
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md#build-and-ci)). To build locally anyway you need CMake ≥ 3.28,
a C++17 compiler and Git.

```sh
# Engine tests only (fast, no JUCE download)
cmake -S . -B build -DMIDI_FORGE_BUILD_PLUGIN=OFF -DMIDI_FORGE_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure

# Plug-in (VST3) and the plug-in layer tests (add -DMIDI_FORGE_BUILD_CLAP=ON and MidiForge_CLAP for CLAP)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMIDI_FORGE_BUILD_PLUGIN_TESTS=ON
cmake --build build --config Release --target MidiForge_VST3 midi_forge_plugin_tests
```

On Linux the plug-in needs the JUCE dependencies, e.g. on Ubuntu:
`libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libxi-dev libxcomposite-dev libxrender-dev libgl1-mesa-dev`.

Results land in `build/MidiForge_artefacts/Release/VST3`.

## Checks to run before pushing

```sh
pip install clang-format==19.1.7           # the version CI uses
clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')
clang-tidy -p build Source/Engine/*.cpp Source/Engine/Modules/*.cpp Source/Engine/Modulation/*.cpp Tests/Engine/*.cpp
ctest --test-dir build --output-on-failure
```

Format everything in place with `clang-format -i $(git ls-files '*.cpp' '*.h')`.

## Releases

Bump `project(MidiForge VERSION x.y.z)` in `CMakeLists.txt`, update the manual if needed, then push a
tag `vx.y.z`. CI builds and publishes the release.

## Pull requests

- One topic per pull request, with a description of what changed and why.
- CI must be green.
- Code, docs, manual and tests change together.
- Contributions are licensed under the [GNU AGPLv3](LICENSE), like the rest of MIDI Forge.
