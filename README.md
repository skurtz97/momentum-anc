# Momentum ANC

A small GTK 4 / libadwaita app for controlling a Sennheiser **MOMENTUM 4** from Linux:
noise cancellation, transparency, adaptive mode, and battery level. It talks to the
headset directly over Bluetooth (GAIA v3 over RFCOMM), so it needs no account, no
phone, and no network.

The full protocol notes — frame format, every command id, captures from real
hardware — are in [`docs/protocol.md`](docs/protocol.md).

## Features

| control | status |
|---|---|
| Active noise cancellation | on/off |
| Transparency | on/off (the MOMENTUM 4 has no adjustable level) |
| Adaptive noise cancellation | follows the noise around you |
| Battery | read as a percentage |
| Reconnect | header button, after the headset was switched off or out of range |

All of it is verified against a real MOMENTUM 4 (`M4AEBT PRIDE`, firmware 3.38.3).
Every change is written to the headset and read back, so each switch shows what the
device actually accepted.

Not implemented yet: equalizer, sidetone, smart pause, on-head detection, codec or
firmware info, and device-initiated notifications (see [Limitations](#limitations)).

## Requirements

* Linux with BlueZ, and the headset paired (Bluetooth Classic / SPP)
* GTK 4 ≥ 4.16 and libadwaita ≥ 1.6 at runtime
* To build: [Meson](https://mesonbuild.com) ≥ 1.5, Ninja, a C compiler, and the
  development packages:
  * Fedora: `meson gcc gtk4-devel libadwaita-devel bluez-libs-devel`
  * Debian/Ubuntu: `meson ninja-build build-essential libgtk-4-dev libadwaita-1-dev libbluetooth-dev`
  * Arch: `meson gcc gtk4 libadwaita bluez-libs`

## Install

Packages are built from this tree (see [Packaging](#packaging)); there are no hosted
downloads yet.

```sh
# Fedora / openSUSE, from the finished .rpm
sudo dnf install ./momentum-anc-0.1.0-1.fc45.x86_64.rpm

# Debian / Ubuntu
sudo apt install ./momentum-anc_0.1.0_amd64.deb

# Flatpak (after the build step below)
flatpak run io.github.momentum_anc
```

### From source

```sh
meson setup build
meson compile -C build
meson test -C build        # codec tests, no headset needed
sudo meson install -C build
```

For a local, uninstalled run: `./build/src/momentum-anc`.

## Usage

```sh
momentum-anc                      # first paired device whose alias starts with "MOMENTUM"
momentum-anc --device 80:C3:BA:66:BE:5C
```

The header bar shows the connection state; the rows are disabled while disconnected,
and the refresh button reconnects. There is no device picker on purpose: the RFCOMM
control channel is not advertised in the Bluetooth service list, so discovery matches
on the BlueZ alias instead of a UUID.

## Packaging

All three packages install the same four files — the binary, a `.desktop` entry, an
AppStream metainfo file, and a scalable icon — and take their version from
`meson.build`.

### Deb

```sh
./packaging/build-deb.sh          # -> momentum-anc_0.1.0_amd64.deb
```

The script runs `meson install` into a staging directory, writes the control file
(`Depends: libgtk-4-1 (>= 4.16), libadwaita-1-0 (>= 1.6), libbluetooth3`) and lets
`dpkg-deb` build the archive. Needs `dpkg-deb`, `meson`, `ninja`.

### RPM

```sh
./packaging/build-rpm.sh          # needs rpm-build on Fedora
```

The script tars the working tree, stamps the current version into
`packaging/momentum-anc.spec`, and runs `rpmbuild -bb`; `%check` runs `meson test`
during the build. On a non-RPM host the header of the script has a ready-made
`podman run … fedora` line that does the same in a container.

### Flatpak

```sh
flatpak-builder --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.momentum_anc.yaml
flatpak run io.github.momentum_anc
```

The manifest builds the worktree against `org.gnome.Platform//50`. If
`flatpak-builder` is not installed, use the builder app instead:

```sh
flatpak install --user flathub org.flatpak.Builder
flatpak run --user --command=flatpak-builder org.flatpak.Builder \
    --user --install --force-clean build-flatpak \
    packaging/flatpak/io.github.momentum_anc.yaml
```

When the runtime lives in the user installation but the builder looks in the system
one, add `--env=FLATPAK_SYSTEM_DIR=$HOME/.local/share/flatpak` to that command.

The builder app also leaves `/app/bin/flatpak` in the exported launcher, and GLib
drops such an entry completely, so the app never appears in the GNOME app grid (it
shadows the RPM's copy of the same `.desktop` file, which is fine on its own). Fix
the export after installing:

```sh
sed -i 's|^Exec=/app/bin/flatpak |Exec=/usr/bin/flatpak |' \
    ~/.local/share/flatpak/app/io.github.momentum_anc/current/active/export/share/applications/io.github.momentum_anc.desktop
update-desktop-database ~/.local/share/flatpak/exports/share/applications
```

A `flatpak-builder` from the distro writes `Exec=/usr/bin/flatpak …` itself, so
installing that instead avoids the patch.

The sandbox gets three things it needs: `--allow=bluetooth` for the RFCOMM socket,
`--system-talk-name=org.bluez` so it can find the paired headset, and the usual
Wayland/X11 plus DRI arguments for the window. Without the Bluetooth permission the
window opens but the app cannot connect.

## How it works

```text
src/gaia.[ch]     GAIA v3 frame codec and the command table (no I/O, unit tested)
src/bt.[ch]       RFCOMM transport (channel 15) and BlueZ device lookup
src/session.[ch]  worker thread: connect, blocking reads, decode, push to the UI
src/ui.[ch]       libadwaita window and rows
src/main.c        argument parsing, application startup
src/test-gaia.c   assert-based tests for build/parse/decode
data/             .desktop, AppStream metainfo and icon, installed by meson
packaging/        deb/rpm build scripts, rpm spec, flatpak manifest, icon source
docs/protocol.md  the protocol notes
```

One `GThread` per session does the blocking RFCOMM I/O; results reach the main loop
through `g_idle_add`, and UI callbacks hand writes back to the worker under a mutex.
The UI never blocks on the socket.

## Limitations

* One headset at a time, and it must already be paired.
* GAIA sits on RFCOMM **channel 15** on the MOMENTUM 4; other Sennheiser models
  differ. Channel 14 is iAP2 — sending GAIA frames there powers the headset off.
* RFCOMM admits one client: the phone app and this app cannot control the headset at
  the same time.
* Values are read on connect only. Changes made from the phone are not reflected
  until the refresh button is pressed, because the app does not subscribe to
  device notifications.

## Roadmap

* Subscribe to device notifications (`0007` with feature ids) so the UI follows
  changes made from the phone.
* Device picker when several headsets are paired.
* Five-band equalizer (`1000`-`1002`, bass boost `1008`/`1009`).
* Sidetone, smart pause, on-head detection, auto power-off.

## License

MIT — see [LICENSE](LICENSE).
