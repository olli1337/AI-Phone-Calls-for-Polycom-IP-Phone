"""Optional offline speech producer; the native caller does not import this file."""

import argparse
import math
from pathlib import Path
import struct
import wave


SAMPLE_RATE = 8000
MAX_AUDIO_SAMPLES = 120 * SAMPLE_RATE


def ending_beeps():
    """Return a short PCM16 mono cue with silence around two gentle beeps."""
    beep_frames = SAMPLE_RATE * 180 // 1000
    fade_frames = SAMPLE_RATE * 5 // 1000
    # Five-millisecond fades keep the tone boundaries from clicking.
    beep = b"".join(
        struct.pack("<h", round(
            6000 * math.sin(2 * math.pi * 440 * index / SAMPLE_RATE)
            * min(1, index / fade_frames, (beep_frames - 1 - index) / fade_frames)))
        for index in range(beep_frames))
    silence = b"\x00\x00"
    return (silence * (SAMPLE_RATE * 250 // 1000) + beep
            + silence * (SAMPLE_RATE * 120 // 1000) + beep
            + silence * (SAMPLE_RATE * 150 // 1000))


def main():
    parser = argparse.ArgumentParser(description="Generate a caller-compatible WAV with local Piper.")
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--text-file", type=Path, required=True, help="UTF-8 text, up to 4000 characters")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--end-beep", action="store_true",
                        help="Append two short beeps; total audio must still fit within 120 seconds")
    args = parser.parse_args()
    with args.text_file.open("r", encoding="utf-8-sig") as source:
        raw_text = source.read(4001)
    text = raw_text.strip()
    if not text or len(raw_text) > 4000:
        parser.error("Text must contain 1-4000 characters.")

    # Imports are deliberately optional. No Python/voice runtime is involved in calling.
    import numpy as np
    from piper import PiperVoice
    import soxr

    voice = PiperVoice.load(args.model, use_cuda=False)
    ending = ending_beeps() if args.end_beep else b""
    ending_samples = len(ending) // 2
    total_samples = 0
    created = False
    try:
        with args.output.open("xb") as destination:
            created = True
            with wave.open(destination, "wb") as output:
                output.setnchannels(1)
                output.setsampwidth(2)
                output.setframerate(SAMPLE_RATE)
                for chunk in voice.synthesize(text):
                    converted = soxr.resample(chunk.audio_float_array, chunk.sample_rate, SAMPLE_RATE, quality="HQ")
                    total_samples += len(converted)
                    if total_samples + ending_samples > MAX_AUDIO_SAMPLES:
                        raise ValueError("Audio including any ending beeps exceeds 120 seconds; shorten the text.")
                    samples = np.clip(np.rint(converted * 32767), -32768, 32767).astype("<i2")
                    output.writeframes(samples.tobytes())
                output.writeframes(ending)
                total_samples += ending_samples
    except BaseException:
        if created:
            args.output.unlink(missing_ok=True)
        raise
    print(f"Created {total_samples / 8000:.2f}s of 8000 Hz PCM16 mono audio: {args.output}")


if __name__ == "__main__":
    main()
