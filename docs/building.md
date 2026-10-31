# Build, test and package

Requires Windows 11 x64, PowerShell 7 and Git. The build uses one MSYS2 UCRT64 GCC toolchain for both the C++17 application and PJSIP.

## Build

```powershell
git clone https://github.com/olli1337/AI-Phone-Calls-for-Polycom-IP-Phone.git
cd AI-Phone-Calls-for-Polycom-IP-Phone
.\scripts\Build.ps1 -Bootstrap
```

The included source ZIP is an alternative: extract it and run `scripts/Build.ps1 -Bootstrap` from its project directory. Its `upstream/` folder contains the exact dependency archives.

Bootstrap installs an isolated toolchain under `%LOCALAPPDATA%\aiphone-build`, verifies the pinned sources and builds/tests the caller. Later builds use `.\scripts\Build.ps1`. `-BuildRoot C:\SomeCache` selects another cache; its path must have no spaces because of PJSIP's makefiles. The checkout may contain spaces. System PATH is unchanged.

Source `dependencies/manifest.json` records versions, hashes and the MSYS2 base archive. Pacman verifies package signatures; its rolling packages may change on future bootstraps. `dependencies/toolchain-tested.txt` records the tested packages. This supports source/flag reproduction, not bit-for-bit identity across toolchain updates.

PJSIP's GNU build is configured in source `scripts/build-pjsip.sh` and `dependencies/config_site.h`; CMake/Ninja builds the caller against the staged libraries. The selected build uses G.711 PCMU/PCMA and disables device audio, video, unused codecs and external media/crypto dependencies. RTP/RTCP destination switching is disabled. CMake never downloads dependencies. Use a fresh build cache after changing native build flags/toolchains.

## Checks

`Build.ps1` runs the Catch2 core tests. Controlled SIP/RTP tests use a local fixture and never target the phone:

```powershell
$localIp = Read-Host 'Private IPv4 of the PC LAN adapter'
python .\tests\integration.py --exe .\build\aiphone.exe --peer .\build\aiphone_test_peer.exe `
    --ip $localIp --work-dir .\build\integration
```

The fixture first binds the supplied address locally, then uses it for both endpoints. It covers normal/early-media PCMU/PCMA playback, errors, cancellation, snapshots and mutex/restart behavior. Its generated files stay under ignored `build`; the summary is `build/integration/results.json`. CI runs these hardware-free checks. Announce and manually observe real-phone tests separately.

Check first-party C++ formatting:

```powershell
$formatter = Join-Path $env:LOCALAPPDATA 'aiphone-build\msys64\ucrt64\bin\clang-format.exe'
$files = (Get-ChildItem src,tests -Recurse -File | Where-Object Extension -in '.cpp','.hpp').FullName
& $formatter --dry-run --Werror $files
```

## Package

```powershell
.\scripts\Package.ps1
```

The ignored `dist` folder receives a Windows x64 ZIP, SHA-256 checksum and manifest of file hashes/imported DLLs. It contains `aiphone.exe`, example configuration, synthetic test tone, docs/examples, optional speech scripts, license notices and corresponding first-party/upstream sources. Active configuration, recordings, models and build caches are excluded. Root-level Markdown files other than `README.md` are local notes and are excluded from both archives.

Runtime notices include GCC's license/exception, winpthreads and the separate `COPYING.MinGW-w64-runtime.txt`. Rebuild older ZIPs that omitted that notice before distributing them. See [third-party notices](third-party-notices.md) for license details and optional dependency review limits.

Verify a fresh extraction with build tools absent from PATH. The statically linked caller requires only Windows system DLLs. Builds are unsigned; there is no installer. [Acceptance evidence](acceptance.md) records performed checks and remaining device tests.
