# MIDI Forge – User Manual

MIDI Forge is a MIDI effect plug-in (VST3 and CLAP) for Windows and macOS. It sits in front of an
instrument and changes the MIDI on its way there: it can filter notes out, transform them, generate
new ones and add controlled randomness. You build the effect from up to eight **modules** that you
stack in **slots**.

- [Installation](#installation)
- [Where to put it in your DAW](#where-to-put-it-in-your-daw)
- [The window](#the-window)
- [How the chain works](#how-the-chain-works)
- [Modules](#modules)
- [Presets](#presets)
- [Automation](#automation)
- [Panic and stuck notes](#panic-and-stuck-notes)
- [Troubleshooting](#troubleshooting)

---

## Installation

Download the zip for your system from the project's **Releases** page on GitHub (or, for test builds,
from the **Actions** tab → latest successful run → *Artifacts*). Each zip contains both formats; install
whichever your DAW prefers. Bitwig works best with CLAP; Reaper loads both.

| System  | VST3 folder                                   | CLAP folder                                   |
|---------|-----------------------------------------------|-----------------------------------------------|
| Windows | `C:\Program Files\Common Files\VST3\`         | `C:\Program Files\Common Files\CLAP\`         |
| macOS   | `~/Library/Audio/Plug-Ins/VST3/`              | `~/Library/Audio/Plug-Ins/CLAP/`              |

**macOS only:** the plug-in is not notarized by Apple, so macOS blocks it after downloading. Run this
once in Terminal after copying the files:

```sh
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/"MIDI Forge.vst3"
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/CLAP/"MIDI Forge.clap"
```

Then let your DAW rescan its plug-ins.

## Where to put it in your DAW

- **Reaper:** add MIDI Forge to a track's FX chain *above* the instrument. MIDI flows top to bottom.
- **Bitwig Studio:** put MIDI Forge in an instrument's **Note FX** section, or as a device before the instrument.

MIDI Forge does not process audio. Ableton Live cannot host MIDI-effect plug-ins.

## The window

![MIDI Forge window](images/editor.png)

- **Header:** the preset menu and the **Panic** button.
- **Eight slot rows**, each with:
  - the **slot number** and a coloured strip showing the module's category,
  - a **power switch** – a switched-off slot passes everything through unchanged,
  - the **module menu** – pick what this slot does; picking a module resets its knobs to their defaults,
  - a one-line **description** of the module,
  - up to **four knobs** – drag up/down or left/right; **double-click** to reset a knob,
  - **▲ / ▼** buttons that move the module one slot up or down.
- **Footer:** the colour legend: blue = filters, green = transforms, orange = generators, purple = randomizers.

Note names follow the convention **C3 = MIDI note 60** (as in Bitwig and Cubase; Reaper calls it C4 by default).

## How the chain works

Every MIDI event enters slot 1, then goes to slot 2, and so on to slot 8. Each module only sees what
the slots above it let through, so **order matters**: *Chord → Arpeggiator* arpeggiates chords, while
*Arpeggiator → Chord* plays a chord on every arpeggio step.

MIDI Forge takes care that no note gets stuck, even when you change modules while playing:

- Notes always end the way they started, even if you turn a knob while holding them (a transposed note
  is released at the pitch it started on).
- Switching a slot's module, turning a slot on or off, or moving modules releases the notes passing
  through that slot. Keep holding the key and nothing restarts until you press it again.
- SysEx messages pass through untouched; MIDI clock and other system messages pass through every module.

## Modules

The four knobs of a slot are called **A, B, C, D** in your DAW's automation lists; what they mean depends
on the module loaded in the slot.

### Filters (blue)

**Note Range** – passes or blocks notes inside a key range.

| Knob | Range | Meaning |
|------|-------|---------|
| Low  | C-2 … G8 | Lowest key of the range |
| High | C-2 … G8 | Highest key of the range (Low and High may be swapped) |
| Mode | Pass inside / Block inside | Keep only notes in the range, or remove them |

**Velocity Range** – passes or blocks notes by how hard they were played.

| Knob | Range | Meaning |
|------|-------|---------|
| Min  | 1 … 127 | Lowest velocity of the range |
| Max  | 1 … 127 | Highest velocity of the range |
| Mode | Pass inside / Block inside | Keep or remove notes in the range |

**Channel Filter** – keeps or removes one MIDI channel. Applies to every channel message (notes, CC, pitch bend …).

| Knob | Range | Meaning |
|------|-------|---------|
| Channel | 1 … 16 | The channel |
| Mode | Only this / All except | Keep only this channel, or remove it |

**Event Blocker** – blocks whole kinds of MIDI events.

| Knob | Range | Meaning |
|------|-------|---------|
| Notes | Pass / Block | Notes (and their poly aftertouch) |
| CC | Pass / Block | Control changes (mod wheel, sustain pedal …) |
| Pitch Bend | Pass / Block | Pitch bend |
| AT / Prog | Pass / Block | Channel aftertouch, poly aftertouch and program changes |

### Transforms (green)

**Transpose** – shifts notes. Notes pushed outside the MIDI range are dropped.

| Knob | Range | Meaning |
|------|-------|---------|
| Semitones | −24 … +24 st | Shift in semitones |
| Octave | −4 … +4 oct | Additional shift in octaves |

**Scale Quantize** – moves every note into a key and scale.

| Knob | Range | Meaning |
|------|-------|---------|
| Root | C … B | Key of the scale |
| Scale | Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harm. Minor, Mel. Minor, Major Pent., Minor Pent., Blues, Whole Tone | The scale |
| Snap | Nearest / Up / Down | Where wrong notes go. *Nearest* picks the lower note when both are equally close |

If two keys land on the same scale note, it keeps sounding until both keys are released.

**Velocity Curve** – reshapes how hard notes are played.

| Knob | Range | Meaning |
|------|-------|---------|
| Curve | −100 … +100 | Negative: soft playing gets louder (more sensitive). Positive: you have to play harder. 0: linear |
| Out Min | 1 … 127 | Velocity of the softest possible note |
| Out Max | 1 … 127 | Velocity of the hardest possible note |
| Fixed | Off, 1 … 127 | If not Off, every note gets exactly this velocity (Curve, Out Min and Out Max are ignored) |

**Channel Remap** – moves events to another channel. Applies to all channel messages.

| Knob | Range | Meaning |
|------|-------|---------|
| From | All, 1 … 16 | Which channel to move (All = every channel) |
| To | 1 … 16 | Target channel |

### Generators (orange)

**Arpeggiator** – plays the keys you hold one after another. While your DAW's transport runs, the steps
lock to the bar grid; when it is stopped, the arpeggiator runs on its own clock (at the project tempo)
and starts the moment you press the first key. The keys you hold are not passed on – only the arpeggio.

| Knob | Range | Meaning |
|------|-------|---------|
| Rate | 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | Step length (T = triplet) |
| Pattern | Up, Down, Up/Down, Random, As Played | Order of the notes. *Up/Down* doesn't repeat the top and bottom note; *As Played* follows the order you pressed the keys |
| Octaves | 1 … 4 | How many octaves the pattern spans |
| Gate | 5 … 100 % | Note length as a share of the step |

Up to 32 keys are tracked at once.

**Chord** – plays a chord on every note.

| Knob | Range | Meaning |
|------|-------|---------|
| Type | Major, Minor, Sus2, Sus4, Dim, Aug, Maj7, Min7, Dom7, Min9, Add9, Power, Octave, 5th + Oct | The chord, built upwards from the played note |
| Inversion | 0 … 3 | Each step moves the lowest chord note up an octave |
| Voicing | Close / Open / Bass −1 Oct | *Open* raises the second-lowest note an octave; *Bass −1 Oct* adds the played note an octave lower |
| Added Vel | 0 … 100 % | Velocity of the added notes relative to the played note |

**Note Echo** – a MIDI delay. The original note plays immediately; each echo comes one *Time* later,
quieter, and optionally transposed. Every echo is as long as the original note.

| Knob | Range | Meaning |
|------|-------|---------|
| Time | 1/32 … 1 Bar (incl. triplets T and dotted D) | Distance between echoes, synced to the tempo |
| Repeats | 1 … 16 | Maximum number of echoes |
| Feedback | 10 … 100 % | Each echo's velocity as a share of the previous one. Echoes stop when they get too quiet |
| Pitch | −12 … +12 st | Transposition added with every echo (e.g. +7 makes a rising cascade of fifths) |

### Randomizers (purple)

**Humanize** – makes playing less mechanical. Each note is delayed by a random amount and its velocity
varied. The note-off is delayed by the same amount, so note lengths stay as played.

| Knob | Range | Meaning |
|------|-------|---------|
| Timing | 0 … 50 ms | Maximum delay (notes are only ever delayed, never moved earlier) |
| Velocity | 0 … 64 | Maximum velocity change up or down |

**Probability** – each note plays with the given chance; the rest are skipped.

| Knob | Range | Meaning |
|------|-------|---------|
| Chance | 0 … 100 % | Probability that a note plays |

**Random Pitch** – randomly transposes some notes. Put a *Scale Quantize* after it to stay in key.

| Knob | Range | Meaning |
|------|-------|---------|
| Range | 0 … 24 st | Largest possible shift |
| Chance | 0 … 100 % | Share of notes that get shifted |
| Mode | Any / Octaves / 5ths + Oct | Which intervals are used |

## Presets

The preset menu in the header (also available as "programs" in your DAW) loads one of these chains.
Loading a preset replaces all eight slots.

| Preset | Chain |
|--------|-------|
| Init | All slots empty |
| Chord Arp | Chord (Min7) → Arpeggiator (1/16, Up/Down, 2 octaves) → Humanize |
| Scale Lock (C Minor) | Scale Quantize (C minor) → Velocity Curve (more sensitive, 20–120) |
| Echo Cascade | Note Echo (1/8 dotted, 6 repeats, +7 st) → Scale Quantize (C minor pentatonic) |
| Generative Dorian | Probability 60 % → Random Pitch (octaves) → Scale Quantize (D Dorian) → Note Echo (1/4 triplet) → Humanize |
| Bass Split to Ch 2 | Note Range (up to B2) → Channel Remap (to channel 2) → Velocity Curve (fixed 100) |

Your own settings are saved with your DAW project. Use your DAW's preset function to save your own chains.

## Automation

Each slot has six automatable parameters: **Slot n Module**, **Slot n On** and **Slot n A … D**.
Your DAW displays A … D in the units of the module currently loaded in that slot, and you can type
values in those units (e.g. `+7 st`, `1/16`, `C3`, `75%`).

Automating **Slot n Module** works, but keep in mind that A … D keep their values when the module changes
(choosing a module in the plug-in window resets them; automation does not).

## Panic and stuck notes

If a note ever hangs (for example because the instrument missed a message), press **Panic**. It releases
every note MIDI Forge has sent and sends *All Notes Off* on all 16 channels.

## Troubleshooting

| Problem | Solution |
|---------|----------|
| macOS says the plug-in is damaged or from an unidentified developer | Run the `xattr` commands under [Installation](#installation) |
| DAW doesn't list MIDI Forge | Check the install folder, then rescan plug-ins |
| No sound | MIDI Forge outputs MIDI only – it must sit *before* an instrument on the same track |
| Arpeggiator or echo at the wrong speed | They follow the project tempo; check the tempo of the project |
| Arpeggiator doesn't follow the bar grid | It only locks to the grid while the transport is running |
