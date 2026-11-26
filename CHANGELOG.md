# Changelog

## Unreleased

- Fixed: the editor now reopens at the window size you last used (it always
  came back at the minimum size).
- Fixed: parameter values with whole-number display (frequencies from 100 Hz,
  times from 10 ms, percentages, pan) showed every decimal after dragging,
  e.g. "150.347 Hz" instead of "150 Hz".

## 1.0.0 – 2026-11-25

First public release.

- 24 bands: Bell, Low/High Shelf, Low/High Cut, Notch, Band Pass, Tilt Shelf,
  Flat Tilt, All Pass; slopes 6 – 96 dB/oct and Brickwall; Stereo / Left /
  Right / Mid / Side placement per band.
- Zero Latency (analog-matched IIR), Natural Phase (oversampled, analog phase)
  and Linear Phase (Low … Maximum resolution) processing.
- Dynamic EQ per band with range, threshold, attack, release and external
  side-chain; draggable range and threshold handles with a live level meter.
- Spectrum analyser (pre / post / side-chain), Spectrum Grab, collision
  detection, overlay of another GKEQ instance.
- EQ Match and EQ Sketch.
- Character modes (Clean / Subtle / Warm), Auto Gain, Gain Scale, output gain,
  pan, phase invert, latency-compensated bypass.
- Undo/redo, A/B, factory and user presets, piano roll, resizable GPU-drawn UI.
- Windows installer with uninstaller (VST3 plug-in + standalone app).
