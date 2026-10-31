"""Exercise real PJSIP signaling/RTP against a bounded local fixture, never a phone."""

import argparse
from array import array
import ctypes
import json
import math
import os
from pathlib import Path
import queue
import socket
import struct
import subprocess
import tempfile
import threading
import time
import wave


def hidden_startup():
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    return startup


def write_tone(path, seconds=3):
    with wave.open(str(path), "wb") as output:
        output.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
        output.writeframes(b"".join(
            struct.pack("<h", int(7000 * math.sin(2 * math.pi * (440 + (i // 8000) * 220) * i / 8000)))
            for i in range(int(seconds * 8000))))


def stop_process(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def send_break(process):
    # A private, hidden console makes cancellation independent of the test host's console.
    kernel = ctypes.windll.kernel32
    kernel.FreeConsole()
    if not kernel.AttachConsole(process.pid):
        raise OSError("Could not attach to the hidden caller console")
    try:
        kernel.SetConsoleCtrlHandler(None, True)
        if not kernel.GenerateConsoleCtrlEvent(1, process.pid):
            raise OSError("Could not send CTRL_BREAK to caller")
        time.sleep(0.1)
    finally:
        kernel.FreeConsole()
        kernel.AttachConsole(ctypes.c_ulong(-1).value)
        kernel.SetConsoleCtrlHandler(None, False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--peer", type=Path, required=True)
    parser.add_argument("--ip", required=True, help="An IPv4 address assigned to THIS computer")
    parser.add_argument("--work-dir", type=Path, required=True)
    args = parser.parse_args()
    args.exe = args.exe.resolve()
    args.peer = args.peer.resolve()
    work = args.work_dir.resolve()
    work.mkdir(parents=True, exist_ok=True)
    # Refuse any address not assigned here: this suite cannot dial the physical VVX.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
        probe.bind((args.ip, 0))
    environment = dict(os.environ)
    environment["PATH"] = str(Path(os.environ["SystemRoot"]) / "System32")
    results = []

    with tempfile.TemporaryDirectory(prefix="aiphone-tests-", dir=work) as temporary:
        temp = Path(temporary).resolve()
        assert temp.parent == work and temp.name.startswith("aiphone-tests-")
        audio = temp / "ää sound.wav"
        config = temp / "config.json"

        def configuration(timeout=5):
            return {"schema_version": 1, "phone_ip": args.ip, "local_ip": args.ip,
                    "phone_sip_port": 5064, "phone_user": "testpeer",
                    "local_sip_port": 5062, "rtp_port": 40000, "rtcp_port": 40001,
                    "answer_timeout_seconds": timeout, "media_ready_timeout_seconds": 1,
                    "shutdown_grace_seconds": 1}

        def caller_command(caller_name="TEST AGENT"):
            return [str(args.exe), "call", "--config", str(config), "--audio", str(audio),
                    "--caller", caller_name, "--json"]

        def parsed_result(process, output):
            value = json.loads(output)
            assert value["exit_code"] == process.returncode, (value, process.returncode)
            assert value["schema_version"] == 1
            assert "teardown_confirmed" not in value
            return value

        def run_case(mode, expected, code, cancel=None, mutate=False):
            write_tone(audio)
            config.write_text(json.dumps(configuration(2 if mode == "silent" else 5)), encoding="utf-8")
            recording = temp / (mode + (cancel or "") + ".wav")
            peer_log = temp / (mode + (cancel or "") + ".log")
            with peer_log.open("w", encoding="utf-8") as diagnostics:
                peer = subprocess.Popen(
                    [str(args.peer), "--ip", args.ip, "--mode", mode, "--record", str(recording)],
                    stdout=subprocess.PIPE, stderr=diagnostics, text=True, encoding="utf-8",
                    creationflags=subprocess.CREATE_NO_WINDOW, startupinfo=hidden_startup(), env=environment)
                lines = queue.Queue()

                def read_ready():
                    for line in peer.stdout:
                        lines.put(line.strip())
                    lines.put(None)

                reader = threading.Thread(target=read_ready, daemon=True)
                reader.start()
                caller = None
                try:
                    deadline = time.monotonic() + 5
                    while True:
                        line = lines.get(timeout=max(0.01, deadline - time.monotonic()))
                        if line == "READY":
                            break
                        assert line is not None, "Peer exited before ready"
                    flags = (subprocess.CREATE_NEW_CONSOLE | subprocess.CREATE_NEW_PROCESS_GROUP
                             if cancel else subprocess.CREATE_NO_WINDOW)
                    caller = subprocess.Popen(caller_command(), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                              text=True, encoding="utf-8", env=environment,
                                              startupinfo=hidden_startup(), creationflags=flags)
                    if mutate:
                        time.sleep(0.5)
                        write_tone(audio, 0.1)
                    if cancel:
                        time.sleep(0.5 if cancel == "before" else 2.2)
                        send_break(caller)
                    output, errors = caller.communicate(timeout=20)
                    value = parsed_result(caller, output)
                    assert value["outcome"] in expected, (mode, value, errors)
                    assert caller.returncode == code, (mode, value, errors)
                    assert value["local_cleanup_complete"], (mode, value, errors)
                    peer.wait(timeout=5)
                    if code == 0:
                        assert value["answered"] and value["playback_sent"]
                        with wave.open(str(recording), "rb") as received:
                            assert received.getframerate() == 8000
                            samples = array("h", received.readframes(received.getnframes()))
                        nonzero = [i for i, sample in enumerate(samples) if abs(sample) > 500]
                        assert nonzero, "Peer received no audio"
                        audible_span = (nonzero[-1] - nonzero[0]) / 8000
                        assert 2.94 <= audible_span <= 3.1, audible_span
                        if mode == "early":
                            assert nonzero[0] / 8000 >= 1.2, "Audio leaked before the answer"
                    else:
                        assert not value["playback_sent"], value
                    label = mode + ("-cancel-" + cancel if cancel else "") + ("-snapshot" if mutate else "")
                    results.append({"case": label, "outcome": value["outcome"], "exit_code": code})
                    print("PASS", label, value["outcome"], flush=True)
                finally:
                    if caller is not None:
                        stop_process(caller)
                    stop_process(peer)
                    peer.stdout.close()
                    reader.join(timeout=2)

        run_case("answer", {"completed"}, 0)
        run_case("early", {"completed"}, 0)
        run_case("pcma", {"completed"}, 0)
        run_case("busy", {"phone_busy"}, 3)
        run_case("reject", {"rejected"}, 4)
        run_case("silent", {"no_answer"}, 4)
        run_case("hangup", {"remote_hangup"}, 6)
        run_case("hold", {"media_failed", "media_lost"}, 5)
        run_case("bad-media", {"media_failed"}, 5)
        run_case("redirect", {"rejected"}, 4)
        run_case("silent", {"cancelled"}, 6, cancel="before")
        run_case("answer", {"cancelled"}, 6, cancel="after")
        run_case("answer", {"completed"}, 0, mutate=True)

        # Force-kill an unanswered caller, then prove local contention/recovery.
        config.write_text(json.dumps(configuration(10)), encoding="utf-8")
        write_tone(audio)
        first = subprocess.Popen(caller_command(), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                 env=environment, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            time.sleep(0.4)
            second = subprocess.run(caller_command(), capture_output=True, text=True, encoding="utf-8",
                                    env=environment, timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
            value = parsed_result(second, second.stdout)
            assert value["outcome"] == "local_busy" and second.returncode == 3, value
        finally:
            stop_process(first)
            first.communicate(timeout=3)
        ready = subprocess.run([str(args.exe), "doctor", "--config", str(config), "--json"],
                               capture_output=True, text=True, encoding="utf-8", env=environment,
                               timeout=5, creationflags=subprocess.CREATE_NO_WINDOW)
        assert parsed_result(ready, ready.stdout)["outcome"] == "ready"
        results.append({"case": "mutex-contention-and-forced-kill-recovery", "outcome": "ready"})
        print("PASS mutex contention and forced-kill recovery", flush=True)

        invalid = subprocess.run(caller_command("BAD\r\nFrom: evil"), capture_output=True,
                                 text=True, encoding="utf-8", env=environment, timeout=5,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
        assert parsed_result(invalid, invalid.stdout)["outcome"] == "invalid_input"
        assert invalid.returncode == 2
        results.append({"case": "CLI-header-injection", "outcome": "invalid_input"})

    summary = {"passed": len(results), "cases": results,
               "environment": "Windows x64, system-only PATH, local synthetic PJSIP peer"}
    (work / "results.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print(f"Passed {len(results)} controlled integration cases.", flush=True)


if __name__ == "__main__":
    main()
