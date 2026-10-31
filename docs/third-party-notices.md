# Third-party notices

AI Phone Calls for Polycom IP Phone's first-party code is GPL-3.0-or-later. Copyright (c) 2026 AIPhone contributors. The release is a combined GPL-compatible executable, not an MIT/BSD-only product.

The source manifest records exact upstream versions, URLs and hashes. Release `licenses/` contains applicable license texts; the accompanying `AI-Phone-Calls-for-Polycom-IP-Phone-source.zip` includes the exact upstream archives/header and first-party build recipe. Upstream notices in those sources are retained.

| Component | Selected version | Terms / attribution |
|---|---|---|
| PJSIP/PJSUA2 and selected PJPROJECT sublibraries | 2.17 | GPL-2.0-or-later; Teluu Inc., Benny Prijono and upstream contributors; see `licenses/PJSIP-COPYING` and source notices |
| CLI11 | 2.5.0 | BSD-3-Clause; Henry Schreiner and upstream contributors; `licenses/CLI11-LICENSE` |
| JSON for Modern C++ | 3.12.0 | MIT; Niels Lohmann; `licenses/json-LICENSE.MIT` |
| dr_wav | commit `39ba1ae7945d562ed18db5bba89298343c121222` | MIT-0 option; David Reid; complete dual-license statement retained in the supplied `dr_wav.h` |
| GCC libgcc / libstdc++ runtime code | Tested toolchain recorded separately | GPL-3.0-or-later with GCC Runtime Library Exception; Free Software Foundation and contributors; `licenses/gcc/` |
| MinGW-w64 C runtime | Tested toolchain recorded separately | Component-specific terms retained in full in `licenses/COPYING.MinGW-w64-runtime.txt`; mingw-w64 project and upstream contributors |
| MinGW-w64 winpthreads | Tested toolchain recorded separately | MIT-style and upstream notices; mingw-w64 project; `licenses/winpthreads-COPYING` |
| Catch2 (tests only) | 3.8.1 | Boost Software License 1.0; upstream contributors; included with test sources, not linked into `aiphone.exe` |

The selected PJSIP build excludes external codec/media/crypto libraries. Any retained bundled source notices are available in the complete PJPROJECT archive. Windows system DLLs are supplied by the operating system and are not redistributed here.

Build tools (MSYS2, GCC, CMake, Ninja, GNU utilities, Git, LLVM, PowerShell and Python) retain their own component licenses. They are not bundled in the Windows runtime ZIP. GCC runtime code benefits from its upstream runtime exception; its notice is included. The MinGW-w64 runtime notices are copied from the toolchain used for packaging, separately from the winpthreads notice. See the [upstream MinGW-w64 packaging guidance](https://github.com/mingw-w64/mingw-w64/blob/master/COPYING.MinGW-w64/COPYING.MinGW-w64.txt).

## Compatibility and review scope

The selected native dependencies are compatible with the project's GPL-3.0-or-later distribution. PJSIP's GPL-2.0-or-later grant permits using it under GPLv3; see [PJSIP's licensing guidance](https://docs.pjsip.org/en/2.17/overview/license_faq.html). Each dependency retains its own license and applicable attribution requirements. Compatibility does not replace those notices or the obligation to provide corresponding source for the combined GPL executable.

The native inventory above, pinned source archives, build flags and recorded toolchain describe the selected caller build. Build/test tools have separate licenses; using them does not require every tool to have the project's license. The optional Python environment and voice require their own review before redistribution. This inventory is not an exhaustive audit of every component bundled inside optional third-party wheels.

## Optional speech producer

The optional Python packages and voice are downloaded separately by the user and are not part of the native caller or bundled binaries. Direct packages are Piper 1.8.0 (GPL-3.0-or-later) and python-soxr 0.5.0.post1 (LGPL-2.1-or-later, including its libsoxr terms). The tested transitive versions include ONNX Runtime CPU, NumPy and embedded eSpeak NG; retain the packages' own license metadata if redistributing that environment.

See [audio and voice notes](audio.md) for voice source/revision, matching file hashes and model-card information. The selected model card identifies a public-domain training dataset but does not clearly state a license for the model weights; model redistribution rights remain unverified. Do not infer a model's terms from an engine or dataset license. No speech model, generated speech, firmware or provisioning backup is redistributed by this project's release package. The mathematical test tone may be reused without restriction.
