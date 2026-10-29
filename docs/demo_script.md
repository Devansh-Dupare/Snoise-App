# Demo video script

Prepare first: `python3 tools/make_test_audio.py`, then build and open the app. A real speech recording
with fan or hiss noise makes a better demo than the synthetic file; use `TestAudio/noisy_synthetic.wav` as
the fallback. Keep to about 3 minutes.

- [ ] **Load.** Open `noisy_synthetic.wav` with File > Open Audio (Cmd+O) or by dropping it on the window. Show the waveform and spectrogram, and press Play to hear the noise.
- [ ] **Noise region.** Drag across the first 1.5 s of the waveform. Point out the noise profile plot (a
      smooth hiss shape plus a spike at 120 Hz). Mention that File > Open Noise File (or dropping on the Noise profile card) with `noise_only.wav` gives
      the same profile.
- [ ] **State the settings.** N = 2048, H = 512 (75 % overlap), periodic Hann analysis and synthesis, α = 1.0 (the default).
- [ ] **Process.** Press Process; the button shows progress. Show the processed spectrogram next to the original.
- [ ] **A/B.** Play, then switch Original / Processed while it plays; the position is kept.
- [ ] **α comparison.** Set α = 3.0 (use `]` or the knob) and process again; point out the thinner sound and any musical noise.
- [ ] **Zoom.** Cmd + scroll on the waveform: the waveform and both spectrograms zoom together.
- [ ] **Save.** File > Export Processed Audio (Cmd+E) and, if time allows, reopen it in another editor.
- [ ] **Errors (optional).** Select a region under 0.1 s, or load a noise file with a different sample rate.
