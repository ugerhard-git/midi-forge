# Ideas and planned features

Features that were discussed but not built yet, so nothing gets lost. Each entry says its status:

- **Planned** – design agreed with the owner, waiting for the go to implement.
- **Idea** – worth doing later; needs a design discussion first.

When a feature is implemented, remove it here and describe it in [MANUAL.md](MANUAL.md) and
[ARCHITECTURE.md](ARCHITECTURE.md).

---

## Planned

### LFO modulation (4 LFOs, 8 routes)

Modulate slot parameters without DAW automation.

- **4 global LFOs**, each with:
  - **Wave:** Sine, Triangle, Saw Up, Saw Down, Square (with **pulse width**), Sample & Hold
    (random step per cycle), Smooth Random (gliding random)
  - **Sync:** Free (rate in Hz, about 0.01–20 Hz) or Tempo (8 bars … 1/32, incl. dotted and triplets)
  - **Phase:** 0–360°
  - **Restart on first key** (optional): the LFO restarts when the first key is pressed after all keys
    were released, for repeatable sweeps
  - Tempo-synced LFOs lock to the bar grid while the transport runs and keep running at project tempo
    when it is stopped (same behaviour as the Arpeggiator)
- **8 modulation routes**: source (LFO 1–4), target (parameter A–D of any slot), amount (−100 … +100 %).
  The LFO swings the parameter around the knob's value; ±100 % can sweep the whole range; results are
  clamped. Choice parameters step through their choices.
- Not modulatable: module type and slot on/off (changing them releases notes).
- Modulation never writes back into the host parameters, so it does not fight DAW automation.
  All LFO and route settings are themselves automatable host parameters (appended, so existing projects
  stay compatible).
- LFOs are evaluated at each event's exact sample position (not once per block); real-time safe.
- **UI:** tabs "Chain | Modulation". Modulated knobs show a second ring with the current modulated value.
- **Tests:** waveform shapes, pulse width, tempo lock to the grid, phase, restart on key, amount scaling
  and clamping, deterministic randomness with fixed seeds, random LFO routings in the stress test,
  plug-in tests for the new parameters and state.

## Ideas

### Modulating the modulation

Let routes target another LFO's rate or another route's amount (e.g. a slow LFO that speeds up a fast one).
Build after the LFO feature exists.
