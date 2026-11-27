# GKEQ
<img width="1638" height="960" alt="image" src="https://github.com/user-attachments/assets/3cef8b6e-f2c3-42e6-b444-d9e603075aaa" />

24-band parametric / dynamic EQ, VST3 + standalone, Windows x64. GPLv3.

## Install

Run `GKEQ x.y.z Setup.exe` from [Releases](https://github.com/DevoArn/GKEQ/releases), rescan plug-ins (maker **GK**). Silent: `/S`.

| Path | Content |
|---|---|
| `C:\Program Files\Common Files\VST3\GKEQ.vst3` | Plug-in |
| `C:\Program Files\GKEQ\` | Standalone, uninstaller, licences |
| `Documents\GKEQ\Presets` | User presets |

Upgrades work while a DAW has the plug-in loaded (old file moved aside, new one used after restart).

## Build

Windows 10/11 x64, VS 2019/2022 (or Build Tools) with *Desktop development with C++*; Python 3 for the installer only.

```
git clone --recursive https://github.com/DevoArn/GKEQ.git
build.bat            Release VST3 + standalone  -> build\ModernEQ_artefacts\Release\
build.bat installer  + installer                -> dist\GKEQ x.y.z Setup.exe
build.bat tests      DSP tests
build.bat hosttest   VST3 in a test host
build.bat snapshot   UI test, renders build\ui_*.png
build.bat debug      Debug build
powershell -ExecutionPolicy Bypass -File Tests\installer_test.ps1   (after installer)
```

Submodules in `external\`: JUCE 7.0.12 (`build.bat` lowers its CMake minimum to 3.20) and PFFFT. Installer: `Installer\` (JUCE app, zip payload from `Tools\make_payload.py`). CI: `.github/workflows/ci.yml` – build + all tests on every push; a `v*` tag publishes a release.

## Features

**Bands** (up to 24): frequency 10 Hz – 30 kHz, gain ±30 dB, Q 0.025 – 40, enable, solo.
- Shapes: Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt Shelf, Flat Tilt (constant dB/oct 20 Hz – 20 kHz), All Pass.
- Slopes 6 / 12 / 18 / 24 / 30 / 36 / 48 / 72 / 96 dB/oct and Brickwall (ideal in Linear Phase, 144 dB/oct otherwise); true higher-order shelves; Q adds resonance to cuts and shelves.
- Placement: Stereo, Left, Right, Mid, Side.

**Modes**
- Zero Latency: magnitude-matched IIR (matched-z poles, numerator fitted to the analog response, best-of-candidates near Nyquist), 0 smp.
- Natural Phase: same filters 4x oversampled (2x at 88.2/96 kHz) via linear-phase half-bands, analog magnitude + phase, 78 smp.
- Linear Phase: windowed FIR from the analog target each hop, FFT convolution, cross-faded kernels; 2048 – 32768 smp (scaled with sample rate).

**Dynamic EQ** per band: range ±30 dB (compress/expand), threshold, attack, release, internal / external side-chain, band-limited detector; look-ahead in Linear Phase.

**Other**: Character Clean / Subtle / Warm (2x oversampled), output gain, pan, phase invert, Auto Gain, Gain Scale −100 … 200 %, click-free latency-compensated bypass.

**Analyser**: pre / post / side-chain, 60 / 90 / 120 dB, resolution, speed, tilt 0 – 6 dB/oct, freeze, Spectrum Grab, Collision Detection, overlay of another GKEQ instance (same host process).

**EQ Match** (side-chain or another instance, up to 24 bells/shelves, amount + smoothing) and **EQ Sketch** (draw a curve → bands).

**Workflow**: undo/redo, A/B, factory + user presets, piano roll, OpenGL, floating band panel, multi-select, 369 automatable parameters.

## Controls

| Action | Result |
|---|---|
| Double-click empty / node | New band / bypass |
| Drag node (Ctrl: Q, Shift: fine) | Frequency + gain |
| Wheel / Ctrl+wheel on node | Q / dynamic range |
| Alt+click node, Delete | Delete band |
| Drag bar above/below a dynamic node | Dynamic range |
| Drag **T** marker | Threshold (triangle = live level) |
| Drag empty area | Rubber-band selection |
| Ctrl+A, Esc, arrows | Select all, deselect, nudge |
| Ctrl+Z / Ctrl+Y | Undo / redo |

## Verification

- Zero Latency 4-band curve vs analog: max error 0.02 dB (20 Hz – 20 kHz, 48 kHz).
- Natural Phase: max error 0.001 dB; latency reported = measured (78); null −73 dB with no bands.
- Linear Phase: impulse symmetric about the reported latency; 40 Hz error 0.8 dB (High) to 0.05 dB (Maximum).
- All shapes / slopes / Qs at 44.1 / 48 / 96 kHz: stable, mid-band error ≤ 0.55 dB extreme, < 0.05 dB typical.
- Dynamics: −12 dB range → −12.00 dB loud, 0.00 dB quiet. Side-only bands leave mono untouched (−129 dB).
- CPU (8 bands, steep cuts, M/S, dynamic, 48 kHz stereo): 0.4 % Zero Latency, 1.9 % Natural, 0.2 – 0.3 % Linear.
- Host test: side-chain bus, bit-transparent flat, latency 0 ↔ 4096, state round trip.

## Limitations

No spectral dynamics, threshold learning, surround or MIDI learn; other instances viewable, not editable; Zero Latency above ~17 kHz with high Q at 44.1/48 kHz can deviate a few dB near Nyquist (use Natural Phase).

## Licence

GPL-3.0-or-later, © 2026 Roman Gutekunst (GK). No warranty. Uses [JUCE](https://juce.com) 7 (GPLv3), VST3 SDK (Steinberg, GPLv3; VST is a trademark of Steinberg Media Technologies GmbH) and [PFFFT](https://github.com/marton78/pffft) (BSD-style).
