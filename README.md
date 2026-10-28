# MIDI Forge

A modular MIDI filter and effect plug-in. Stack up to eight modules – filters, transforms, generators
and randomizers – in front of any instrument.

![MIDI Forge](docs/images/editor.png)

- **Format:** VST3 · **System:** Windows
- **Made for:** Reaper and Bitwig Studio (any host that supports MIDI-effect plug-ins should work)
- **20 modules:** Note Range, Velocity Range, Channel Filter, Event Blocker, Note Split, Transpose, Scale Quantize,
  Velocity Curve, Channel Remap, Note Dispatcher, Groove, Arpeggiator, Chord, Note Echo, Repeater (ratchets), Humanize,
  Probability, Random Pitch, Pitch Slide, Mutate
- **4 LFOs, 4 Random modulators, 4 step modulators and 8 modulation routes** move any module setting or switch
  slots on and off: LFOs tempo-synced or free with 7 waveforms; Random modulators pick a new value per played
  note or chord; step modulators play 4–16 drawn steps in time with the song; bipolar or unipolar per route
- **Your own presets**, saved as files you can back up and share, plus 12 factory presets
- **Module browser with search** and built-in help for every module and knob
- No stuck notes, even when you change modules while playing

## Download

- **Releases:** the *Releases* section of this repository.
- **Latest build:** *Actions* tab → latest green *Build* run → *Artifacts* → `MIDI-Forge-Windows`.

Installation and usage: **[User manual](docs/MANUAL.md)**.

## Development

The plug-in is built entirely by GitHub Actions – nothing needs to be built locally.

- [CONTRIBUTING.md](CONTRIBUTING.md) – ground rules, building, checks, releases
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) – how the code is organised
- [docs/IDEAS.md](docs/IDEAS.md) – planned features and ideas
- [SECURITY.md](SECURITY.md) – reporting vulnerabilities

Built with [JUCE](https://juce.com).
