"""Generate a rights-free test signal. Never plays sound or calls the phone."""

import argparse
import math
from pathlib import Path
import struct
import wave


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seconds", type=int, default=6, choices=range(1, 121), metavar="1..120")
    args = parser.parse_args()
    with args.output.open("xb") as destination:
        with wave.open(destination, "wb") as output:
            output.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
            for second in range(args.seconds):
                frequency = 440 if second % 2 == 0 else 660
                output.writeframes(b"".join(
                    struct.pack("<h", int(7000 * math.sin(2 * math.pi * frequency * i / 8000)))
                    for i in range(8000)))
    print(args.output)


if __name__ == "__main__":
    main()
