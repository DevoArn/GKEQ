# Contributing to GKEQ

Thanks for your interest! Bug reports, ideas and pull requests are welcome.

## Reporting bugs

Open an issue with:

- your Windows version and plug-in host (DAW) incl. version,
- sample rate / buffer size and processing mode (Zero Latency, Natural or Linear Phase),
- steps to reproduce, and what you expected vs. what happened,
- a saved preset (`Documents\GKEQ\Presets\*.meqpreset`) if the problem depends on the settings.

## Building

```
git clone --recursive https://github.com/DevoArn/GKEQ.git
cd GKEQ
build.bat
```

Requirements: Windows 10/11 x64, Visual Studio 2019 or 2022 (or the Build
Tools) with the *Desktop development with C++* workload. `build.bat` uses the
CMake and Ninja bundled with Visual Studio. Python 3 is only needed for
`build.bat installer`.

## Before opening a pull request

Run the checks that apply to your change:

| Command | What it checks |
|---|---|
| `build.bat tests` | DSP accuracy against the analog prototypes, latency, null tests, dynamics, CPU |
| `build.bat hosttest` | The built VST3 in a test host: parameters, latency switching, state |
| `build.bat snapshot` | Renders the editor to `build\ui_*.png` for a visual check |
| `build.bat installer` + `Tests\installer_test.ps1` | Installer: install, upgrade while loaded, uninstall |

Please keep pull requests focused, describe what you changed and why, and say
which of the checks you ran.

## Code style

- C++17, JUCE style: 4-space indentation, braces on their own line, a space
  before the parenthesis of function calls (`foo (x)`).
- No allocations, locks or file access on the audio thread.
- New parameters must keep the existing parameter IDs and plug-in codes
  unchanged so saved projects keep loading.
- Every source file starts with the GPL header (see existing files).

## Licence

By contributing you agree that your contribution is licensed under the
GNU General Public License v3 or later, like the rest of GKEQ.
