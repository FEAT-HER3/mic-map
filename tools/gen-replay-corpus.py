#!/usr/bin/env python3
"""Phase 9 / TEST-04 / CONTEXT D-35: regenerate the seed replay corpus.

Produces three deterministic 48 kHz / mono / 16-bit PCM WAVs under
tests/corpus/replay/:

    positive_001.wav         ~2 s — band-limited white-noise burst
    negative_silence_001.wav  5 s — silence / very-low-amplitude noise floor
    negative_speech_001.wav   5 s — synthetic speech-like signal

All RNGs are seeded explicitly so the script is deterministic — re-running
on any host produces byte-identical files. This guarantees git-tracked
corpus stability per CONTEXT D-34 (replay determinism invariant).

Usage:
    python tools/gen-replay-corpus.py
    # writes tests/corpus/replay/{positive_001,negative_silence_001,negative_speech_001}.wav
"""
from __future__ import annotations

import argparse
import pathlib
import wave

import numpy as np

CORPUS_DIR = pathlib.Path(__file__).resolve().parent.parent / "tests" / "corpus" / "replay"
SAMPLE_RATE = 48000


def write_wav_int16(path: pathlib.Path, samples_float: np.ndarray) -> None:
    """Quantise to int16 and write a canonical 48 kHz mono WAV."""
    clipped = np.clip(samples_float, -1.0, 1.0)
    pcm = (clipped * 32767.0).astype(np.int16)
    path.parent.mkdir(parents=True, exist_ok=True)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)        # 16-bit
        w.setframerate(SAMPLE_RATE)
        w.writeframes(pcm.tobytes())


def gen_positive_001() -> np.ndarray:
    """~2 s mic-cover-like burst: silence -> band-limited white noise -> silence.

    Designed to mimic the energy + spectral-flatness signature the FFT
    detector trains against when a finger covers the mic. The white noise
    is generated with a fixed seed for byte-stable output.
    """
    duration_s = 2.0
    n = int(SAMPLE_RATE * duration_s)
    out = np.zeros(n, dtype=np.float32)

    burst_start_s = 0.2
    burst_end_s = 1.8
    s0 = int(SAMPLE_RATE * burst_start_s)
    s1 = int(SAMPLE_RATE * burst_end_s)

    rng = np.random.default_rng(seed=0xC0FFEE)
    burst = rng.standard_normal(s1 - s0).astype(np.float32)
    burst /= max(1.0, np.max(np.abs(burst)))      # normalise to ~unity peak
    burst *= 0.7                                  # leave headroom

    # Soft 5 ms attack / release so the boundaries don't generate a click
    # transient that the detector might latch on to spuriously.
    attack_n = int(0.005 * SAMPLE_RATE)
    fade = np.linspace(0.0, 1.0, attack_n, dtype=np.float32)
    burst[:attack_n] *= fade
    burst[-attack_n:] *= fade[::-1]

    out[s0:s1] = burst
    return out


def gen_negative_silence_001() -> np.ndarray:
    """5 s of silence with tiny dither so dr_wav reads f32 = 0.0 cleanly."""
    duration_s = 5.0
    n = int(SAMPLE_RATE * duration_s)
    # Intentionally exactly zero — int16 0 sample bytes; deterministic.
    return np.zeros(n, dtype=np.float32)


def gen_negative_speech_001() -> np.ndarray:
    """5 s of synthetic speech-like signal (multi-formant, syllable-rate AM).

    Designed to NOT trigger the white-noise detector — speech is far from
    spectrally flat, so high spectral flatness should not be observed.
    """
    duration_s = 5.0
    n = int(SAMPLE_RATE * duration_s)
    t = np.linspace(0.0, duration_s, n, endpoint=False, dtype=np.float64)

    # 3 Hz syllable-rate envelope.
    envelope = 0.5 * (1.0 + np.sin(2.0 * np.pi * 3.0 * t)) * 0.4

    # Three-formant synthesis: 200 Hz fundamental + 800 Hz + 2400 Hz peaks.
    formants = (
        np.sin(2.0 * np.pi * 200.0 * t)
        + 0.7 * np.sin(2.0 * np.pi * 800.0 * t)
        + 0.5 * np.sin(2.0 * np.pi * 2400.0 * t)
    )

    signal = (envelope * formants * 0.3).astype(np.float32)
    return signal


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--out-dir",
        type=pathlib.Path,
        default=CORPUS_DIR,
        help=f"Output directory (default: {CORPUS_DIR})",
    )
    args = parser.parse_args()

    items = (
        ("positive_001.wav", gen_positive_001()),
        ("negative_silence_001.wav", gen_negative_silence_001()),
        ("negative_speech_001.wav", gen_negative_speech_001()),
    )

    for name, samples in items:
        target = args.out_dir / name
        write_wav_int16(target, samples)
        print(f"wrote {target}  ({len(samples)} samples, {len(samples)/SAMPLE_RATE:.2f} s)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
