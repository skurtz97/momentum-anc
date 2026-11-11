# GAIA v3 over Bluetooth Classic — notes

Everything here was verified against a real **MOMENTUM 4** (model id
`M4AEBT PRIDE`, firmware reports `3.38.3` as three u16s) on Linux/BlueZ, unless marked
otherwise. Other Sennheiser models share the command set but not every behaviour
noted below.

## Transport

The headset speaks Qualcomm **GAIA v3** on an RFCOMM (Serial Port Profile) channel.
On the MOMENTUM 4 the record is SDP service class
`a2129ff3-081b-4c45-8afe-469d9c4842ec` and it sits on **RFCOMM channel 15**.

* The UUID is *not* advertised in the EIR data — `bluetoothctl info` shows only the
  standard audio profiles plus the iAP2 vendor UUID. So device discovery in this
  project matches on the BlueZ `Alias` (`MOMENTUM…`) instead of the UUID.
* **Never** connect to RFCOMM channel 14. That is Apple iAP2; sending GAIA frames
  there powers the headset off (reported by the `sennheiser-desktop-client` author,
  not reproduced here).
* One RFCOMM connection is enough for all commands; no pairing/handshake is needed
  beyond the Bluetooth pairing itself.
* Opening the channel when the headset is off/out of range fails with
  `No route to host`, and on a powered-down adapter with `NotReady`
  (`br-connection-adapter-not-powered`).

## Frame format

```text
offset  size  field
0       1     0xFF            start of frame
1       1     0x03            flags (GAIA v3)
2       2     payload length  big-endian, payload only, excludes the 8-byte header
4       2     vendor id       0x0495 Sennheiser
6       2     command id      big-endian
8       n     payload
```

Command-id convention:

| kind                | value              |
|---------------------|--------------------|
| request             | `id`               |
| response            | `id \| 0x0100`     |
| async notification  | `id \| 0x0080`     |
| error               | `id \| 0x0180`     |

Implemented in `src/gaia.c` (`gaia_build`, `gaia_parse`, `gaia_set_field`,
`gaia_get_field`, `gaia_decode`).

## What the app writes

| UI row      | request on change        | read on connect |
|-------------|--------------------------|-----------------|
| ANC         | `1A04 [0/1]`             | `1A05`          |
| Transparency| `1A00 [02, 0/1]`         | `1A01`          |
| Adaptive    | `1A00 [03, 0/1]`         | `1A01`          |
| Bass boost  | `1008 [0/1]`             | `1009`          |
| EQ band n   | `1001 [n, gain]`         | `1002 [n]`      |
| Battery     | —                        | `0603`          |
| Firmware    | —                        | `1201`          |

One `1A01` reply updates both the transparency and adaptive rows, which is why
`gaia_decode()` returns a list of field updates rather than one.

## Commands this app uses

All rows below were exercised end-to-end against the headset; the capture column is
verbatim.

| feature                | request                    | response | capture |
|------------------------|----------------------------|----------|---------|
| Battery %              | GET `0603`, no payload     | `0703` → `[pct]` | `FF 03 00 00 04 95 06 03` → `FF 03 00 01 04 95 07 03 5A` (90 %) |
| ANC on/off             | SET `1A04` `[0/1]`         | `1B04`, empty | `FF 03 00 01 04 95 1A 04 01` → `FF 03 00 00 04 95 1B 04` |
| ANC on/off             | GET `1A05`, no payload     | `1B05` → `[0/1]` | readback follows the write |
| Noise-control modes    | SET `1A00` `[mode, state]` | `1B00`, empty | `FF 03 00 02 04 95 1A 00 03 01` (adaptive on) |
| Noise-control modes    | GET `1A01`, no payload     | `1B01` → 3 × `[mode, state]` | `01 02 02 00 03 01` |
| Transparency level     | SET `1A02` `[0-100]`       | `1B02` | accepted, but see below |
| Transparency level     | GET `1A03`                 | `1B03` → `[lvl]` | always `00` on this firmware |
| Firmware version       | GET `1201`, no payload     | `1301` → 3 × u16 | `FF 03 00 00 04 95 12 01` → `00 03 00 26 00 03` (3.38.3) |
| Model id               | GET `1206`, no payload     | `1306` → string | `4D 34 41 45 42 54 20 50 52 49 44 45 00` ("M4AEBT PRIDE") |
| Equalizer range        | GET `1000`, no payload     | `1100` → `[bands, min, max]` | `05 C4 3C 00 00` → 5 bands, −60…+60 |
| Bass boost             | SET `1008` `[0/1]`         | `1108`, empty | readback `1109` → `[0/1]` |
| Bass boost             | GET `1009`, no payload     | `1109` → `[0/1]` | `01` when on |
| EQ band gain           | SET `1001` `[band, gain]`  | `1101`, empty | `FF 03 00 02 04 95 10 01 00 E2` (−3 dB) |
| EQ band gain           | GET `1002` `[band]`        | `1102` → `[gain]` | `FF 03 00 01 04 95 11 02 E2` |

### Equalizer replies carry no band number

`1102` returns the gain alone, so a client that asks for several bands has to pair
each reply with the request that produced it, in order. The app keeps a queue of the
outstanding band ids for exactly that reason. Gains are signed millibels (dB × 10) in
one byte, which is why the captured −3 dB reads back as `E2` (−30).

`1001` also accepts a payload with no value (`[band]`), which resets the band.

### Noise-control mode ids (`1A00` / `1B01`)

The reply payload is three `(mode, state)` pairs:

| mode | meaning      | states                                    |
|------|--------------|-------------------------------------------|
| 1    | anti-wind    | 0 = off, 1 = max, 2 = auto                |
| 2    | transparency | 0 = off, 1 = on                           |
| 3    | adaptive     | 0 = off, 1 = on                           |

Verified reads:

```text
start                        01 02 02 00 03 01   anti-wind auto, transparency off, adaptive on
after SET 1A00 [02 01]       01 02 02 01 03 01   transparency on
after SET 1A00 [02 00]       01 02 02 00 03 01   transparency off
```

Anything not listed is echoed back unchanged, so a single `(mode, state)` pair is a
legal write.

### Transparency level is dead on the MOMENTUM 4

`1A02` is acknowledged (`1B02`) and `1A03` always answers `00`, in every combination
tried: ANC on, ANC off, transparency mode on, level written before and after the mode
change. Conclusion: the MOMENTUM 4 has a fixed transparency level and only an
on/off toggle (mode id 2). The adjustable level belongs to other Sennheiser models
(ACCENTUM family), which the reference docs below confirm. This project therefore
drives transparency through `1A00 [02, state]`, not through `1A02`.

### No notifications are subscribed

The headset can push state changes (`1A81` mode, `1A83` transparency, `1A85` ANC,
`0683` battery) after the app registers interest with
`0007 [featureID]` (battery = 3, ANC = 13, transparentHearing = 12, …).

This app does not register, so values are read once on connect and whenever the user
presses *Reconnect*. No background traffic, nothing to unsubscribe. Subscribing is
the obvious next step if the UI should follow changes made from the phone app.

## Commands documented but not implemented

Verified to exist in the Sennheiser GAIA stack; useful if the UI grows.

| feature              | request / response | note |
|----------------------|--------------------|------|
| Codec in use         | `0800` → `0900`    | one byte, e.g. 5 = aptX HD |
| Sidetone 0-5         | `0805` / `0806` → `0905` / `0906` | |
| Smart Pause          | `080C` / `080D` → `090C` / `090D` | `[0/1]` |
| On-head detection    | `0400` / `0401` → `0500` / `0501` | `[0/1]` |
| Auto-answer          | `080A` / `080B` → `090A` / `090B` | `[0/1]` |
| Comfort call         | `0814` / `0815` → `0914` / `0915` | `[0/1]` |
| Auto power-off       | `0600` / `0601` → `0700` / `0701` | `[timerID 0, value_u16]` |
| Adaptive EQ / sound profile | `0402` / `0403` → `0502` / `0503` | 0 = adaptive, 1 = neutral, 2 = bass |

## Sources

* `github.com/Oein/sennheiser-desktop-client/blob/main/PROTOCOL.md` — APK analysis,
  frame format, command catalog, notification feature ids.
* `codeberg.org/galen/momentumctl` — independent MOMENTUM 4 CLI; same command ids
  for ANC/adaptive/anti-wind/transparency level.
* This repository's own probes, run against the headset over RFCOMM 15.
