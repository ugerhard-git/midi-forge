# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Roadmap

Agreed order (28 Oct 2026). Each step: design discussion, explicit go, build.

1. ~~Step modulator ties (0.16)~~ built
2. ~~Groove (0.17), with a shared "position in the bar" helper~~ built
3. ~~Strum + Ghost Notes (0.18)~~ built
4. ~~Beat Thinning + Diatonic Harmonizer (0.19), time signatures followed everywhere~~ built
5. ~~Modulating the modulation (0.20)~~ built
6. ~~Pitch Slide: Fall Off (0.21)~~ built
- Built in 0.22: the forge look of the header. Parked: the macOS build (see below; GitHub had no Mac runners
  available on release day), Pitch Slide MPE, CLAP build.

## Planned

### Note Echo: Pre-Delay

Agreed with the owner on 30 Oct 2026. A pause before the echoes start.

- **E "Pre-Delay"** (page 2), default *Off*. With *Tempo*: Off, 1/64 … 1 Bar (incl. T and D; the list of
  Pitch Slide's Delay; a bar is a real bar of the time signature). With *ms*: Off, 1 … 1000 ms.
- **F "Pre-Delay Sync"**: *Tempo* / *ms*, default *Tempo*.
- The whole echo series moves later: the first echo comes after Pre-Delay + Time, the following ones every
  Time after that. The original note still plays immediately.
- Both knobs can be modulated; like Time, the value is read when a note starts and applies to its echoes.
- Echoes keep their length (they end as long after the original note-off as they started after the note-on);
  no latency.
- Both knobs `addedInLayout (4)`, `currentKnobLayoutVersion` 4, so older projects get *Off* / *Tempo*.

### Bounce (new Generator)

Agreed with the owner on 30 Oct 2026. A delay whose repeats follow a ball bouncing on the floor: the gaps get
shorter and the hits softer, by the same factor each time.

- **Physics:** the played note is the first impact and plays immediately. A ball dropped from height h hits
  the floor at speed v = √(2gh); each bounce keeps the share e of its speed (the material's coefficient of
  restitution). Gap k = 2 · v · e^k / g, i.e. every gap is e times the previous one. Each bounce's velocity is
  the played velocity × e^k. Example: 1 m, e = 0.8 → gaps of about 0.72, 0.58, 0.46, 0.37 s …, about 3.6 s in
  all.
- **Knobs (page 1):**
  - *Height*: 5 cm … 5 m (used with Height Sync *Height*, dimmed otherwise).
  - *Material*: Super Ball, Rubber, Basketball, Tennis Ball, Wood, Glass, Metal, Clay; each sets a value of e,
    tuned by ear.
  - *Bounciness*: fine adjustment of the material's e (about ±20 points, e kept within 5 … 95 %).
  - *Bounces*: Auto, 1 … 32. *Auto* stops when the next gap would be shorter than about 20 ms or the velocity
    would drop below 1; a number stops after that many bounces at the latest.
- **Knobs (page 2):**
  - *Height Sync*: *Height* (real metres, independent of the tempo) / *Tempo* (the first gap is a note value,
    the rest follow the physics).
  - *First Gap*: 1/32 … 1 Bar (used with *Tempo*, dimmed otherwise; a bar is a real bar of the time signature).
    Only the first gap follows the beat; the bounces then speed up freely, as a ball does.
  - *Pitch per Bounce*: −12 … +12 st, added with every bounce (like Note Echo's Pitch).
  - *Gravity*: Earth, Moon, Mars, Jupiter … scales the timing with Height Sync *Height* (dimmed with *Tempo*).
- **Notes:** each bounce note lasts a share of the gap to the next bounce, so same-pitch bounces never
  overlap and every note-on has its note-off; a chord bounces together; values are read when a note starts
  (also when modulated); the number of pending events stays within the scheduler's limit.
- Uses `isParameterInactive()` to dim Height / Gravity or First Gap depending on Height Sync.

## Ideas

### macOS build

Added and removed again in 0.22.0: GitHub's macOS runners were not available (jobs cancelled after 15 minutes
in the queue, "capacity constraints"), and the owner did not want the release to wait. To bring it back,
add to the `build` job's matrix `- name: macOS` / `os: macos-latest`, and after "Collect plug-ins":
ad-hoc sign and check (`codesign --force --deep --sign -`, `codesign --verify --deep`, `lipo -info`), then
zip with `ditto -c -k --sequesterRsrc` in the job and upload that zip as the artifact (and add a download
step for `MIDI-Forge-macOS` in the release job) (a GitHub artifact
loses the bundle's file permissions, which breaks the signature); the release job then takes the zip as it
is. Put the macOS install folder (`~/Library/Audio/Plug-Ins/VST3/`) and the quarantine note
(`xattr -dr com.apple.quarantine …`) back into the manual and packaging/INSTALL.txt. Consider an Intel macOS
runner as a fallback when Apple Silicon runners are scarce (the universal binary builds on either).

### CLAP build

Switched off in 0.9.0. `-DMIDI_FORGE_BUILD_CLAP=ON` brings it back; then add the CLAP target and file to the
build and collect steps of the workflow (and sign it on macOS), and add its install folders to the manual and
packaging/INSTALL.txt.

### Pitch Slide: MPE

Discussed while planning the Pitch Slide (0.10.0): an MPE mode that gives every note its own channel, so
notes of a chord can slide (and fall off) individually.
