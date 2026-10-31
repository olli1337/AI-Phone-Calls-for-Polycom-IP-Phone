# AI Phone Calls for Polycom IP Phone

A Windows command-line tool that rings a **Polycom/Poly VVX 301**, plays a WAV after you answer, and hangs up. Any local AI project can launch `aiphone.exe`.

Generate your own voice messages from text with the included optional local text-to-speech recipe.

Supports Windows 11 x64 and one configured phone on the same LAN. File playback works offline with no PBX, cloud account, background service, or Python runtime.

## Quick start

1. [Build and package from source](docs/building.md), then extract the Windows ZIP. Run the commands below from its extracted folder.
2. Copy `config.example.json` (`config/config.example.json` in a checkout) to a private folder outside the repository.
3. Replace `<PHONE_LAN_IPV4>` and `<PC_LAN_IPV4>` with the phone and PC's actual private LAN addresses. Review the [device setup](docs/device-setup.md).
4. Run a passive check, then place a call:

```powershell
$config = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'AIPhone\config.json'
.\aiphone.exe doctor --config $config --audio .\test-tone.wav --json
.\aiphone.exe call --config $config --audio .\test-tone.wav --caller 'MY AGENT' --json
```

Answer manually to hear the six-second test tone. `doctor` validates local prerequisites; it does not contact the phone or prove reachability. The example configuration intentionally fails validation until its address placeholders are replaced.

For your own audio, pass `--audio 'C:\My Project\message.wav'`. Supported input: **PCM16 little-endian, mono, 8,000 Hz RIFF/WAVE**, up to 120 seconds and 2 MiB. The original file is preserved; Unicode and spaced paths are supported.

## Generate your own voice messages

Create custom spoken messages from your own text using **Piper text-to-speech**. The included recipe runs on CPU and produces a WAV ready for the caller, with optional end-of-message beeps. Setup requires Python and an initial download of packages and a voice model; later generation works offline.

Follow the [speech setup guide](docs/audio.md#offline-speech) and configure the phone first. The complete [Speak-And-Call.ps1 example](examples/Speak-And-Call.ps1) writes your text, generates speech with a unique filename, and calls the phone. Replace the package path below with your extracted folder:

```powershell
.\examples\Speak-And-Call.ps1 -AppDirectory 'C:\Tools\AIPhone' `
    -Message 'Hello! Your AI agent is calling. Time for a coffee break. Goodbye.'
```

By default, speech setup and the example use your Documents folder's `AIPhone` directory for the voice, Python environment, `config.json` and generated files. Use `-DataDirectory` to select another private folder prepared with `Setup-Speech.ps1 -Destination`.

## Integrate

Generate the WAV first, then launch `aiphone.exe call` with an argument list. [Python](examples/call.py) and [PowerShell](examples/Call.ps1) wrappers are included. The process waits for completion and returns one JSON object with `--json`; diagnostics go to stderr.

Only one call runs at a time, with no automatic retry. Playback starts after confirmed answer and usable media. The caller does not open desktop microphones/speakers or record phone audio. SIP/RTP on the LAN is unencrypted.

A successful result proves local playback and cleanup; it cannot prove that the listener heard every packet or that the phone received BYE. See the [CLI contract](docs/cli.md) and [observed test results](docs/acceptance.md).

## Documentation

- [Device configuration and troubleshooting](docs/device-setup.md)
- [CLI, JSON results and integration](docs/cli.md)
- [Optional offline speech and audio conversion](docs/audio.md)
- [Build, tests and packaging](docs/building.md)

## License

GPL-3.0-or-later. [Third-party notices](docs/third-party-notices.md) describe dependency licenses and runtime notices. Binary packages include corresponding sources. Optional voice models are excluded; the example model's redistribution rights remain unverified.

This project is not affiliated with Poly/HP, OpenAI or Anthropic.
