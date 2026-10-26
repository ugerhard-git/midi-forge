# CLAUDE.md – working rules for AI assistants

MIDI Forge is a modular MIDI effect plug-in (VST3 + CLAP, Windows + macOS) built with JUCE 9 and CMake.
It is built exclusively by GitHub Actions; the owner does not build locally.

Read first: [CONTRIBUTING.md](CONTRIBUTING.md) (all ground rules apply to you), then
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md). The user-facing behaviour is described in
[docs/MANUAL.md](docs/MANUAL.md).

## Owner's rules (keep this list complete; add new rules here when the owner states them)

1. Code must be readable by humans: understandable, generous names for variables, classes, functions.
2. Comment what code does, in Doxygen style so IDEs show it.
3. Write documentation and keep it true after every change.
4. Write a user manual and keep it true after every change.
5. Use code formatting tools and linters (clang-format, clang-tidy, warnings as errors).
6. Keep security and performance in mind for every change.
7. Write tests whenever possible or sensible, and keep them up to date.
8. When the owner asks about a possible new feature: first discuss and plan, then **wait for an explicit
   go** before writing code or building.
9. Record new ground rules from the owner in this file and/or CONTRIBUTING.md.
10. Features that are discussed but postponed go into [docs/IDEAS.md](docs/IDEAS.md) (agreed designs as
    *Planned*, open ideas as *Idea*), so nothing gets forgotten. Remove an entry once it is built.

## Checklist for every change

- [ ] Names are descriptive; new code has doc comments.
- [ ] Tests added or updated (`Tests/Engine` for engine behaviour, `Tests/Plugin` for the JUCE layer).
- [ ] `docs/MANUAL.md` updated for anything a user can see or hear.
- [ ] `docs/ARCHITECTURE.md` updated for structural, build or CI changes.
- [ ] Audio-thread code stays real-time safe (no allocation, locks or I/O).
- [ ] Parameter IDs, `ModuleType` order and choice label order unchanged (append only).
- [ ] `clang-format` applied; engine tests pass locally if a compiler is available.
- [ ] After pushing, check that the GitHub Actions run is green.

## Useful commands

```sh
cmake -S . -B build -DMIDI_FORGE_BUILD_PLUGIN=OFF -DMIDI_FORGE_WARNINGS_AS_ERRORS=ON && cmake --build build && ctest --test-dir build --output-on-failure
clang-format -i $(git ls-files '*.cpp' '*.h')
```
