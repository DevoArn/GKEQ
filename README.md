# GKEQ
<img width="1638" height="960" alt="image" src="https://github.com/user-attachments/assets/3cef8b6e-f2c3-42e6-b444-d9e603075aaa" />

A free, open-source 24-band parametric / dynamic EQ plug-in for Windows
(VST3, x64), by GK: analog-matched zero-latency filters, Natural and Linear
Phase modes, per-band dynamics with external side-chain, mid/side placement,
spectrum analyser with grab and collision detection, EQ Match and EQ Sketch.
Works in any VST3 host (DAW) on Windows; a standalone app is included.

It has its own name and visual design and is not affiliated with any
commercial product.

---

## Install

1. Download **`GKEQ 1.0.0 Setup.exe`** from the
   [Releases](https://github.com/DevoArn/GKEQ/releases) page (or build it, see below)
   and run it (accept the Windows admin prompt). Choose the components (VST3 plug-in, standalone app, Start
   Menu shortcuts) and press *Install*.
2. Rescan plug-ins in your DAW. Most hosts scan the standard VST3 folder
   automatically; some need it enabled in their plug-in settings.
3. GKEQ appears with the maker name **GK**.

| Installed to | |
|---|---|
| `C:\Program Files\Common Files\VST3\GKEQ.vst3` | VST3 plug-in (folder selectable) |
| `C:\Program Files\GKEQ\` | Standalone app, uninstaller, licence and notices |
| Start Menu → *GKEQ* | Shortcuts to the app and the uninstaller |
| `Documents\GKEQ\Presets` | Your presets (created on first save, kept on uninstall unless you tick the option) |

Uninstall from *Settings → Apps → Installed apps → GKEQ* or the Start Menu.
The installer can run while a DAW is open: a plug-in file the host has loaded
is moved aside and the new version is used after the host restarts. Silent use:
`"GKEQ 1.0.0 Setup.exe" /S` and `"Uninstall GKEQ.exe" /S`.

Side-chain: route a side-chain signal to GKEQ's second input in your host,
then enable *EXT SC* on a dynamic band (or show the side-chain spectrum from
the *Analyzer* menu).

## Build

Clone with the dependencies: `git clone --recursive https://github.com/DevoArn/GKEQ.git` (or run
`git submodule update --init --depth 1`; `build.bat` also does this
automatically on a fresh clone).

Requirements: Windows 10/11 x64 and Visual Studio 2019 or 2022 (any edition,
or the Build Tools) with the *Desktop development with C++* workload;
`build.bat` finds it automatically and uses its bundled CMake and Ninja.
Python 3 is needed for `build.bat installer` only.

```
build.bat            Release VST3 + Standalone        -> build\ModernEQ_artefacts\Release\
build.bat installer  Release build + installer        -> dist\GKEQ 1.0.0 Setup.exe
build.bat tests      DSP test-suite (accuracy, latency, null tests, CPU)
build.bat hosttest   Loads the built VST3 in a test host (params, latency, state)
build.bat snapshot   Renders the editor to build\ui_*.png
build.bat debug      Debug build
```

Installer test (install into a temp folder, byte-compare, reinstall while the
DLL is loaded, uninstall): `powershell -ExecutionPolicy Bypass -File Tests\installer_test.ps1`
after `build.bat installer`. The installer source is in `Installer\` (a JUCE app
that embeds the build as a zip payload made by `Tools\make_payload.py`).

Dependencies live in `external\` as git submodules: JUCE 7.0.12 (`build.bat`
lowers its `cmake_minimum_required` from 3.22 to 3.20 so the CMake shipped
with VS 2019 works too) and
PFFFT (SIMD FFT used for linear-phase convolution and the analyser).

---

## Features

**Bands**
- Up to 24 bands, each with Shape, Frequency (10 Hz – 30 kHz), Gain (±30 dB),
  Q (0.025 – 40), Slope, Stereo Placement, Enable, Solo.
- Shapes: Bell, Low Shelf, Low Cut, High Shelf, High Cut, Notch, Band Pass,
  Tilt Shelf, Flat Tilt (constant dB/oct over 20 Hz – 20 kHz), All Pass.
- Slopes 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct and Brickwall (ideal in
  Linear Phase, 144 dB/oct in the minimum-phase modes). Shelves have true
  higher-order slopes; Q adds resonance to cuts and shelves.
- Stereo placement per band: Stereo, Left, Right, Mid, Side (separate L/R/M/S
  curves are drawn when used).

**Processing modes**
- *Zero Latency* – magnitude-matched IIR filters (matched-z poles, numerator
  fitted to the analog response; for sections near Nyquist the best of several
  candidate designs is chosen). No cramping near Nyquist, 0 samples latency.
- *Natural Phase* – the same filters run 4x oversampled (2x at 88.2/96 kHz)
  through custom linear-phase half-band filters with exact integer latency, so
  magnitude **and** phase follow the analog prototypes. 78 samples latency.
- *Linear Phase* – the analog target response is turned into a windowed
  linear-phase FIR every hop and applied by FFT convolution; kernel changes
  cross-fade (automation and dynamics stay smooth). Resolutions Low … Maximum
  = 2048 … 32768 samples latency (scaled with the sample rate).

**Dynamic EQ** – per band (gain-type shapes): Range (±30 dB, compress or
expand), Threshold, Attack, Release, internal or external side-chain
detection through a band-limited detector. The live gain movement is shown on
the curve. In Linear Phase mode the detector works as look-ahead.

**Character** – Clean / Subtle / Warm analog-style saturation, 2x oversampled.

**Output** – Output gain, pan, phase invert, Auto Gain (curve-based loudness
compensation), Gain Scale (−100 … 200 % of all band gains), latency-compensated
click-free bypass.

**Analyser** – pre-EQ, post-EQ and side-chain spectra; range 60/90/120 dB;
resolution Low…Maximum; speed; tilt 0 – 6 dB/oct; freeze; *Spectrum Grab*
(hover a peak, drag it to create a band); *Collision Detection* (red areas where
this track and the side-chain/other instance compete); overlay of **another
GKEQ instance's** spectrum (all instances in the same host process see each other).

**EQ Match** – learns the average spectrum of this track and a reference
(side-chain input or another instance), shows reference / current /
difference, and fits up to 24 bells/shelves (amount and smoothing adjustable;
re-matching replaces the previous match bands).

**EQ Sketch** – press *Sketch*, draw a curve with the mouse; bands that
reproduce it are created on release.

**Workflow** – undo/redo, A/B with copy, factory + user presets (saved to
`Documents\GKEQ\Presets`), resizable window (sizes in *Settings*),
piano-roll display with note names, GPU (OpenGL) drawing (toggle in
*Settings*), floating band panel, multi-band selection and editing, context
menus, all parameters automatable (369 parameters).

### Mouse & keyboard

| Action | Result |
|---|---|
| Double-click empty area | New band (Bell; Low/High Cut near the edges) |
| Drag node | Frequency + gain (Q for cuts / notch / band pass) |
| Ctrl+drag / Shift+drag | Adjust Q / fine adjustment |
| Wheel on node, Ctrl+wheel | Q, dynamic range |
| Double-click node / Alt+click node | Enable-bypass / delete |
| Drag small bar above/below a dynamic node | Dynamic range (always grabbable, even at 0 dB) |
| Drag the **T** marker of a dynamic band | Threshold (drawn in the analyser's level scale; the triangle is the band's live level, orange when above the threshold) |
| Right-click node / empty area | Band menu / display menu |
| Drag on empty area | Rubber-band selection, then drag bands together |
| Delete, Ctrl+A, Esc, arrows | Delete, select all, deselect, nudge |
| Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z | Undo, redo |

---

## Verification (from `build.bat tests` / `hosttest`)

- Zero Latency, typical 4-band curve vs analog target: max error 0.02 dB (20 Hz – 20 kHz, 48 kHz).
- Natural Phase: max error 0.001 dB; reported latency = measured latency (78);
  null vs. dry −73 dB with no bands.
- Linear Phase: impulse exactly symmetric about the reported latency; error at
  40 Hz from 0.8 dB (High) down to 0.05 dB (Maximum).
- All shapes/slopes/Qs at 44.1/48/96 kHz: every section stable, mid-band
  matching error ≤ 0.55 dB even for extreme settings (typical settings < 0.05 dB).
- Dynamics: −12 dB range gives −12.00 dB on a loud tone, 0.00 dB on a quiet one.
- Side-only bands leave mono material untouched (−129 dB / bit-exact).
- CPU with 8 bands incl. steep cuts, M/S and a dynamic band, 48 kHz stereo:
  0.4 % (Zero Latency), 1.9 % (Natural Phase), 0.2 – 0.3 % (Linear Phase) of one core.
- VST3 host test: loads, sidechain bus present, bit-transparent when flat,
  latency switches 0 ↔ 4096, state round-trips.

## Known limitations / not implemented

- Spectral dynamics (frequency-selective dynamics inside one band) and
  automatic threshold learning are not implemented.
- Other instances can be viewed (spectrum overlay, collisions, EQ Match source)
  but not edited remotely; there is no surround/Atmos support and no built-in
  MIDI learn (use your host's MIDI mapping of plug-in parameters).
- In Zero Latency mode at 44.1/48 kHz, bands placed above ~17 kHz with high Q
  can deviate a few dB from the analog curve in the last half octave below
  Nyquist (use Natural Phase there).
- The plug-in has been verified with automated tests and a JUCE test host.

## Contributing

Bug reports and pull requests are welcome, see [CONTRIBUTING.md](CONTRIBUTING.md)
and the [changelog](CHANGELOG.md).

## Licence

GKEQ is free software: you can redistribute it and/or modify it under the
terms of the **GNU General Public License version 3 or later** (see [LICENSE](LICENSE)).
Copyright (C) 2026 Roman Gutekunst (GK).
It is distributed WITHOUT ANY WARRANTY.

Third-party components:

- [JUCE](https://juce.com) 7 – GPLv3.
- VST3 SDK (included with JUCE) – Steinberg Media Technologies GmbH, used under
  GPLv3. VST is a trademark of Steinberg Media Technologies GmbH, registered in
  Europe and other countries.
- [PFFFT](https://github.com/marton78/pffft) – BSD-style licence
  (`external/pffft/LICENSE.txt`).

The installer ships `LICENSE.txt` and `Third-Party Notices.txt` (with a link to
this source code) next to the standalone app.
