# Security policy

## Reporting a vulnerability

Please report security problems privately through GitHub: **Security** tab → **Report a vulnerability**.
Do not open a public issue for them. You will get an answer as soon as possible.

## What counts

MIDI Forge runs inside music software and reads data from host projects and MIDI input. Examples of
relevant problems: crashes or memory errors caused by crafted project state or MIDI data, or anything
in the build and release pipeline that could let someone tamper with the published downloads.

## How the project protects itself

- Plug-in state from host projects is validated before use; parameter values are clamped.
- The engine is tested under address and undefined-behaviour sanitizers in CI.
- Dependencies are pinned to exact versions/commits; GitHub Actions are pinned to commit SHAs.
- Workflows run with read-only permissions; only the release job can write.
