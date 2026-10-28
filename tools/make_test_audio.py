#!/usr/bin/env python3
"""Generates synthetic demo audio into TestAudio/ (standard library only).

  noisy_synthetic.wav  - 1.5 s of noise only, then a voice-like harmonic signal buried in the same noise
  noise_only.wav       - a separate noise-only clip with the same character (for "Load noise file")
  clean_synthetic.wav  - the voice-like signal alone, for measuring SNR

All files are mono, 16-bit, 44.1 kHz. Run:  python3 tools/make_test_audio.py
"""
import math
import random
import struct
import wave
from pathlib import Path

FS = 44100
OUT = Path(__file__).resolve().parent.parent / "TestAudio"


def noise(seconds, seed):
    """Steady fan/hiss-like noise: low-passed white noise, a little broadband hiss and 120 Hz hum."""
    rng = random.Random(seed)
    n = int(seconds * FS)
    out, low = [], 0.0
    for i in range(n):
        low += 0.12 * (rng.gauss(0.0, 1.0) - low)               # one-pole low-pass
        hiss = rng.gauss(0.0, 0.25)
        hum = 0.35 * math.sin(2 * math.pi * 120 * i / FS)
        out.append(0.06 * (2.2 * low + hiss + hum))
    return out


def voice(seconds):
    """Harmonic 'syllables' with a gliding pitch: a stand-in for speech."""
    n = int(seconds * FS)
    out, phase = [], 0.0
    for i in range(n):
        t = i / FS
        syllable = max(0.0, math.sin(2 * math.pi * 2.5 * t)) ** 0.7        # ~2.5 syllables per second
        f0 = 140 + 40 * math.sin(2 * math.pi * 0.7 * t)                      # pitch glide
        phase += 2 * math.pi * f0 / FS
        s = sum(math.sin(h * phase) / h for h in range(1, 13))               # 12 harmonics, 1/h roll-off
        out.append(0.22 * syllable * s / 2.5)
    return out


def write(path, samples):
    peak = max(1.0, max(abs(s) for s in samples))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(FS)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1.0, min(1.0, s / peak)) * 32767)) for s in samples))
    print(f"wrote {path.name}: {len(samples) / FS:.1f} s")


if __name__ == "__main__":
    OUT.mkdir(exist_ok=True)
    speech = voice(6.5)
    lead = [0.0] * int(1.5 * FS)
    clean = lead + speech
    bed = noise(8.0, seed=1)
    write(OUT / "clean_synthetic.wav", clean)
    write(OUT / "noisy_synthetic.wav", [c + b for c, b in zip(clean, bed)])
    write(OUT / "noise_only.wav", noise(3.0, seed=2))
