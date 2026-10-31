# Test evidence

Observed on 2026-10-30, with packaging/documentation follow-ups on 2026-10-31. This record separates local test results from handset observations; actual network addresses are omitted.

## Environment and automated checks

- Windows 11 Pro x64, VVX 301 on the same LAN; direct UDP SIP on port 5060 with no user part, registrar or authentication.
- PJSIP 2.17 and MSYS2 UCRT64 GCC 16.2.0. Exact packages/hashes are recorded in source `dependencies/manifest.json` and `dependencies/toolchain-tested.txt`.
- Native build and C++ formatting passed. Catch2 passed **12 cases / 103 assertions** covering configuration/labels, WAV validation, Unicode snapshots, cleanup, mutex exclusion and JSON types.
- **15 controlled SIP/RTP cases** passed against a local fixture, not the phone: PCMU/PCMA, early-media gating, busy/reject/no answer, early hang-up, hold, off-target SDP, redirects, cancellation before/after answer, snapshot isolation, contention/forced-kill recovery and CLI header-injection rejection. Processes used a system-only PATH.
- Optional Piper/soxr synthesis produced valid audio. The end-beep option passed local duration/cleanup/overwrite checks, including the 120-second boundary; it was not tested in an extra handset call.
- PowerShell parsing and Python syntax checks passed. Private logs/audio remain outside published sources.

## Handset observations

An initial unanswered call timed out. A subsequent manually answered call played the full six-second tone, negotiated PCMU and ended normally; the listener confirmed complete playback and hang-up.

| Spoken-call invocation | Caller | Local outcome |
|---|---|---|
| Direct executable | CODEX | `completed`, exit 0 |
| Python wrapper | CLAUDE | `completed`, exit 0 |
| PowerShell wrapper | MY AGENT | `completed`, exit 0 |
| Direct executable, Unicode/spaced WAV path | CODEX | `remote_hangup`, exit 6; EOF not observed |

The listener confirmed expected names, full messages and normal handset endings for all four spoken calls. The fourth call's nonzero result is retained: hearing the message does not establish local EOF.

One further call from a freshly extracted ZIP, with a Unicode/spaced input path and system-only PATH, completed locally. It was answered accidentally, so complete listening was not confirmed. These observations establish five locally completed calls in total (one tone, four speech), not five consecutive fully observed handset acceptances.

No phone, provisioning, account, firmware, auto-answer, router or firewall settings changed. Firmware/admin settings were not inspected, so compatibility evidence covers this device's observed configuration.

## Packaging checks

The original ZIP was extracted outside the checkout, its hashes/source entries inspected, and passive `doctor` plus a real call ran with only Windows System32 on PATH. PE imports required only Windows system DLLs; Python, FFmpeg and MSYS2 were unnecessary for file playback.

The 2026-10-31 licensing follow-up added the separate MinGW-w64 runtime notice. Its hash matched the packaging toolchain in the release folder and ZIP. All 28 manifest file hashes, the ZIP checksum and 51 source entries were verified. A fresh extraction passed `--version` with a system-only PATH. Models, recordings, captures and logs were excluded; the only WAV was the mathematical test tone.

Public documentation was then condensed, address placeholders replaced deployment addresses, and unit tests were given synthetic addresses. This edit adds no handset evidence.

## Limits and remaining checks

Local EOF/cleanup do not prove listening or received BYE. Silent UDP loss can be invisible; force-kill cannot guarantee immediate remote cleanup. Only outgoing file media is connected: the audio-only build never opens desktop sound devices or records phone audio.

Handset digest authentication, provisioning recovery, DND/reject/hold combinations, Windows sleep/network-cable loss and hosted GitHub Actions execution remain unverified. Controlled analogues above are not substitutes for manual hardware tests. Normal SDP is sendrecv; an initial sendonly experiment was treated as local hold and removed, with inbound audio discarded.
