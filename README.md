# Snoise Editor

Snoise Editor is a JUCE 8 desktop app that removes steady background noise from a recording using a noise profile and
magnitude spectral subtraction. See `docs/report.md` for the method and `docs/demo_script.md` for the demo.

## Build

```
cmake -S . -B build
cmake --build build
```

Targets: `SpectralNoiseCancellation` (the app, built as "Snoise Editor.app") and `SNC_Tests` (unit tests).

## Using the app

1. Open a recording with **File > Open Audio** (or drop a file on the window).
2. Mark a noise-only section by dragging on the waveform, or load a noise file with
   **File > Open Noise File** (or drop it on the Noise profile card).
3. Press **Process**, then compare with **Original / Processed**.
4. **File > Export Processed Audio** saves the result as WAV.

Dropping on the waveform or spectrograms loads input audio; dropping on the Noise profile card loads a noise
file (or input audio, if nothing is loaded yet).

## Keyboard shortcuts

On macOS the command key is Cmd; on Windows and Linux it is Ctrl. Every shortcut can be changed in
**Keyboard Shortcuts...** (Cmd+, on macOS, Help menu elsewhere); changes are kept between launches.

| Category | Action | Shortcut |
|---|---|---|
| File | Open audio | Cmd+O |
| | Open noise file | Cmd+Shift+O |
| | Export processed audio | Cmd+E |
| | Close file | Cmd+W |
| Edit | Clear selection | Esc |
| | Clear noise profile | Cmd+Backspace |
| View | Zoom in / out | Cmd+= / Cmd+- |
| | Zoom to fit | Cmd+0 |
| | Zoom to selection | Z |
| | Follow playhead | F |
| | Show / hide spectrograms | Cmd+Shift+S |
| Process | Apply noise reduction (press again to cancel) | Cmd+Return |
| | Alpha up / down by 0.1 | ] / [ |
| | Alpha up / down by 0.01 | Shift+] / Shift+[ |
| | Reset alpha to 1.0 | \ |
| Playback | Play / pause | Space |
| | Stop | . |
| | Return to start | Home or Return |
| | Listen to original / processed | 1 / 2 |
| | Toggle original / processed | Tab |
| App | Keyboard shortcuts | Cmd+, |
| | Quit | Cmd+Q |
| Mouse | Zoom | Cmd + scroll, or pinch |
| | Scroll the timeline | Scroll or two-finger swipe |
| | Mark noise | Drag on the waveform |
| | Seek | Click on the waveform |

Settings (key bindings, recent files, last alpha) are stored in
`~/Library/Application Support/SpectralNoiseCancellation/SpectralNoiseCancellation.settings` on macOS.
Delete that file to return to the defaults.
