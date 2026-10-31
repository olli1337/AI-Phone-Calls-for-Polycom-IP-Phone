# Device and LAN setup

Use a Windows 11 x64 PC and one VVX 301 on the same reachable LAN. Keep addresses in a private configuration file outside Git. DHCP reservations can help keep them stable.

## Configure

Copy the package's `config.example.json` (source: `config/config.example.json`) to a private folder outside the checkout. Replace `<PHONE_LAN_IPV4>` with the phone's address and `<PC_LAN_IPV4>` with the PC's physical LAN adapter address. Both must be literal private IPv4 addresses; VPN, virtual, loopback and public addresses are unsuitable. The placeholders deliberately fail validation.

The tested phone accepted direct UDP SIP on port 5060 with `phone_user` empty and no registrar/authentication. This is evidence for one device configuration, not every firmware/profile. Confirm your phone's direct-call policy and user part. Firmware/admin settings were not inspected or changed during the original tests.

| Setting | Default / constraint |
|---|---|
| `schema_version` | Required, 1 |
| `phone_ip`, `local_ip` | Required private LAN IPv4 addresses |
| `phone_user` | Empty, or up to 64 letters, digits, `_`, `-`, `.` |
| `phone_sip_port`, `local_sip_port` | 5060 / 5062; allowed range 1024–65535 |
| `rtp_port`, `rtcp_port` | 40000 / 40001; even RTP (1024–65534), next port for RTCP, distinct from local SIP |
| `answer_timeout_seconds` | 1–30; default 30 |
| `media_ready_timeout_seconds` | 1–5; default 5 |
| `shutdown_grace_seconds` | 1–5; default 5 |
| `max_audio_seconds` | 1–120; default 120 |

Unknown fields are rejected. If the phone deliberately challenges calls, an optional local `auth` object accepts `username`, `password` and `realm` (default `*`). Digest authentication was not required or tested on the handset. Keep credentials outside Git and off command lines.

Preserve existing accounts/provisioning. Before any necessary phone change, export a backup and record firmware/settings. Avoid factory resets, firmware changes and auto-answer. Restore the saved settings if a change fails.

## Firewall

No firewall change was needed in the original tests. If local policy blocks traffic, use an administrator-approved rule limited to the executable, configured addresses/ports and trusted LAN profile. For example, after setting `$config` to your private file:

```powershell
$settings = Get-Content -LiteralPath $config -Raw | ConvertFrom-Json
$exe = (Resolve-Path .\aiphone.exe).Path
New-NetFirewallRule -DisplayName 'AIPhone - VVX301 only' -Program $exe `
    -Direction Inbound -Action Allow -Protocol UDP -LocalAddress $settings.local_ip `
    -LocalPort @($settings.local_sip_port, $settings.rtp_port, $settings.rtcp_port) `
    -RemoteAddress $settings.phone_ip -Profile Private
```

Match the profile to the trusted LAN's actual policy. Remove this optional rule with `Remove-NetFirewallRule -DisplayName 'AIPhone - VVX301 only'`. Never disable the firewall or expose SIP/RTP through WAN forwarding or UPnP.

## Troubleshooting

| Symptom | Check |
|---|---|
| Address validation fails | Replace placeholders; select the physical LAN IPv4 |
| Port in use / `local_busy` | Wait for the current caller; check other SIP applications |
| Passive check succeeds, no ringing | Phone address/power, DND, SIP port, user part and direct-call policy |
| Answered but silent | WAV format, advertised LAN address, codec and RTP firewall policy |
| `authentication_failed` | Intended phone policy and local credentials |
| Unicode snapshot-folder error | PJSIP needs an ANSI-representable local application-data path |

A hold or early hang-up stops playback and reports failure; start a fresh call explicitly. Logs/captures may contain private audio or credentials and must stay outside Git.
