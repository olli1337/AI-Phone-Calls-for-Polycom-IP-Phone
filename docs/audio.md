# Optional speech and audio conversion

The caller plays existing WAV files without Python, FFmpeg, a model, GPU or Internet service. Generate audio separately with a tool whose terms permit your intended use.

## Offline speech

The optional recipe downloads Piper, python-soxr and one voice during setup. Later synthesis runs locally on CPU. From the checkout or extracted package, using PowerShell 7:

```powershell
.\scripts\Setup-Speech.ps1
$data = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'AIPhone'
'Hello. Your AI agent is calling.' | Set-Content "$data\message.txt" -Encoding utf8
& "$data\tts-env\Scripts\python.exe" .\scripts\synthesize.py `
    --model "$data\voices\en_US-ljspeech-medium.onnx" `
    --text-file "$data\message.txt" --output "$data\message.wav" --end-beep
```

Choose a new output path: existing files are preserved. Input is limited to 4,000 characters; output is PCM16 mono 8,000 Hz, up to 120 seconds. Partial output is removed on failure. Pass the resulting WAV to `aiphone.exe call` as shown in the [quick start](../README.md#quick-start).

For the complete text-to-call workflow, use [Speak-And-Call.ps1](../examples/Speak-And-Call.ps1) after speech setup and phone configuration. It creates unique message/WAV filenames, stops if synthesis fails, then rings the phone and plays after manual answer. Generated files stay in the private data folder. See the [README example](../README.md#generate-your-own-voice-messages) for invocation.

`--end-beep` adds 0.88 seconds: a pause, two gentle 440 Hz beeps with faded edges, and final silence. Omit it for speech only. The complete recording must fit the length limit; the cue is not proof of SIP disconnection.

The tested environment used CPython 3.9.13, Piper 1.8.0, ONNX Runtime CPU 1.19.2, NumPy 2.0.2 and soxr 0.5.0.post1. Exact transitive versions are in [tts-tested-lock.txt](../dependencies/tts-tested-lock.txt). Validate compatible wheels separately for other Python versions.

## Voice provenance

| Item | Selected value |
|---|---|
| Source | [rhasspy/piper-voices](https://huggingface.co/rhasspy/piper-voices) |
| Revision | `b4e358ffcb232591bf73a540e70f7de528e4174a` |
| Voice | `en/en_US/ljspeech/medium/en_US-ljspeech-medium.onnx` |
| Weights SHA-256 | `6f52a751e2349abe7a76735eb09dc1875298c77ea2342ffd2fef79ff81b87f22` |
| Configuration SHA-256 | `141d612cc0a95ed7efc1ca936b845c2364967f2e9217c5dbfcf69fc4d6c65860` |

The [model card](https://huggingface.co/rhasspy/piper-voices/blob/b4e358ffcb232591bf73a540e70f7de528e4174a/en/en_US/ljspeech/medium/MODEL_CARD) identifies a public-domain dataset and training from scratch, but does not clearly license the model weights. Their redistribution rights remain unverified. Hash verification establishes file integrity, not licensing permission. Models and generated speech are excluded from releases.

Piper uses GPL-3.0-or-later; python-soxr/libsoxr use LGPL-2.1-or-later. The separately downloaded environment has not had an exhaustive audit of every bundled wheel component. Review exact wheels, notices and any source obligations before redistributing it. See [third-party notices](third-party-notices.md).

## Convert other audio

An optional FFmpeg CLI can convert MP3 or other WAV formats:

```powershell
ffmpeg -nostdin -i generated.mp3 -vn -ac 1 -ar 8000 -c:a pcm_s16le message.wav
```

FFmpeg is not bundled; its license depends on the build. The included test tone and end beeps are mathematical signals, with no private recording or sound asset.
