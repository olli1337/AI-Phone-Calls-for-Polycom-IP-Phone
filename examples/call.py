"""Invoke AIPhone from any Python project. Standard library only; no SIP SDK."""

import argparse
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--audio", type=Path, required=True)
    parser.add_argument("--caller", default="MY AGENT")
    args = parser.parse_args()
    completed = subprocess.run(
        [str(args.exe.resolve()), "call", "--config", str(args.config.resolve()),
         "--audio", str(args.audio.resolve()), "--caller", args.caller, "--json"],
        capture_output=True, text=True, encoding="utf-8", timeout=175,
    )
    sys.stdout.write(completed.stdout)
    sys.stderr.write(completed.stderr)
    return completed.returncode


if __name__ == "__main__":
    sys.exit(main())
