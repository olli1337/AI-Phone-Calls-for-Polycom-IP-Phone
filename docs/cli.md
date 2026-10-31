# CLI and integration

## Commands

```powershell
.\aiphone.exe doctor --config $config --audio .\message.wav --json
.\aiphone.exe call --config $config --audio .\message.wav --caller 'MY AGENT' `
    --request-id 'task-001' --json
```

`doctor` is passive: it checks configuration, local addresses/ports and optional audio without sending SIP. `call` waits for a single call attempt to finish. `--audio` is required for `call` and optional for `doctor`. `--version` and `--help` are available.

Caller names allow 1–24 ASCII letters, digits, spaces, `_` and `-`, with at least one non-space character; the default is `AI AGENT`. Request IDs allow up to 128 letters, digits, `_`, `-` and `.`, and are correlation labels rather than deduplication keys. Repeating a request makes another call.

Audio must be a regular local RIFF/WAVE file: PCM signed 16-bit little-endian, mono, 8,000 Hz, nonempty, at most 120 seconds and 2 MiB. UNC paths, URLs and unsupported formats are rejected before ringing. The caller snapshots the input without modifying it.

## JSON results

`--json` writes one UTF-8 object to stdout. Diagnostics use stderr. Schema version 1 includes:

| Field | Meaning |
|---|---|
| `schema_version` | Result format version, currently 1 |
| `request_id` | Supplied or generated correlation label |
| `outcome`, `message`, `exit_code` | Specific result, explanation and matching process exit code |
| `answered` | Local SIP stack observed a confirmed call |
| `playback_sent` | Player reached EOF while connected |
| `hangup_requested` | Stack accepted a local end/cancel request |
| `local_cleanup_complete` | Owned resources and temporary audio were released |
| `sip_status` | Final observed INVITE status, or `null` |
| `elapsed_ms` | Total elapsed time |

| Exit code | Result family |
|---|---|
| 0 | Local completion, or passive `doctor` readiness |
| 2 | Invalid command, configuration or audio |
| 3 | Another caller is running, or phone busy/DND |
| 4 | Rejected or no answer |
| 5 | SIP, network, authentication, media or shutdown failure |
| 6 | Cancellation or remote hang-up before EOF |

Use `outcome` to distinguish conditions within a family, such as `local_busy`, `phone_busy`, `rejected`, `no_answer`, `media_timeout` or `remote_hangup`. Force-killing the process may produce no final JSON and can leave the phone waiting for its own timeout.

EOF proves the local player sent its audio, not delivery to the listener. Local disconnection does not confirm a received BYE. A remote hang-up after EOF can complete playback with `hangup_requested` false.

## Launch from another project

Use an argument list and a bounded process timeout:

```python
import json
import subprocess

completed = subprocess.run(
    [r"C:\AIPhone\aiphone.exe", "call",
     "--config", r"C:\Private\config.json",
     "--audio", r"C:\My Project\message.wav",
     "--caller", "MY AGENT", "--request-id", "task-001", "--json"],
    capture_output=True, text=True, encoding="utf-8", timeout=175,
)
result = json.loads(completed.stdout)
```

Complete wrappers: [Python](../examples/call.py), [PowerShell](../examples/Call.ps1). Launch asynchronously in the requesting application if its UI must remain responsive. Ctrl+C/Ctrl+Break requests bounded cancellation; there is no queue, automatic retry or replay.
