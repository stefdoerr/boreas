# Boreas

![Boreas — freeze / infinite-sustain pedal](plugins/Boreas/modgui/screenshot-boreas.png)

**Freeze / infinite-sustain pedal for [MOD Desktop](https://mod.audio/desktop/),
[MOD Dwarf](https://mod.audio/dwarf/), and any VST3 / CLAP / LV2 host.** Capture a
moment of sound and hold it as a smooth, endless drone — stack layers, slide them in
by pitch, shape the tone, and add organic movement. Built with the
[DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF) as a mono-in /
mono-out plugin in **LV2, VST3, and CLAP** formats.

Boreas freezes by **spectral resynthesis**, not looping: it analyses the captured sound
into a bank of steady sine oscillators and plays *those*, so the sustain is dead steady
with no loop seam, no choppiness, and no buzz.

**[Install](#install) · [Manual (PDF)](docs/manual/boreas-manual.pdf) · [Releases](../../releases/latest) · [Building & contributing](DEVELOPERS.md)**

The [PDF manual](docs/manual/boreas-manual.pdf) walks through the three footswitches
and every knob with pictures — the fastest way in.

## Install

Every release ships ready-to-run bundles. Grab the latest for your platform from the
**[Releases page](../../releases/latest)** and follow the matching steps below.

> Replace `X.Y.Z` below with the version you downloaded. The plugin always appears
> under brand **Stefan** as **Boreas**.

### MOD Desktop (Linux)

Download `boreas-vX.Y.Z-linux-amd64.tar.gz`, then copy the `boreas.lv2/` folder into
your MOD Desktop plugin directory (default `~/Documents/MOD Desktop/lv2/`):

```bash
tar xf boreas-vX.Y.Z-linux-amd64.tar.gz
cp -r boreas.lv2 ~/"Documents/MOD Desktop/lv2/"
```

Restart MOD Desktop and the plugin shows up in the plugin store.

### MOD Dwarf (hardware)

Download `boreas-vX.Y.Z-dwarf-aarch64.tar.gz`, copy `boreas.lv2/` onto the device
over the network, and restart its audio stack:

```bash
tar xf boreas-vX.Y.Z-dwarf-aarch64.tar.gz
scp -O -r boreas.lv2 root@192.168.51.1:/root/.lv2/
ssh root@192.168.51.1 'systemctl restart jack2 mod-ui'
```

The plugin then appears in the Dwarf's plugin store.

### Desktop DAWs — VST3 / CLAP (Linux · Windows · macOS)

Download the VST3 + CLAP build for your OS from the Releases page and put the plugin
in your system plugin folder:

| OS | Get / install |
|---|---|
| **Linux** | unpack the VST3 + CLAP archives into `~/.vst3/` and `~/.clap/` |
| **Windows** | unpack into `%COMMONPROGRAMFILES%\VST3\` and `…\CLAP\` |
| **macOS** | run the **unsigned** `.pkg` installer (right-click → Open past Gatekeeper) |

Rescan plugins in your DAW afterwards.

> **Raspberry Pi / Patchbox OS:** LV2 builds for `rpi-aarch64` and `patchbox-os-arm32`
> are on the Releases page too, and Boreas is published on
> [Patchstorage](https://patchstorage.com/) for one-tap install on supported devices.

## Manual

A beginner-friendly **PDF manual** walks through the footswitches, knobs, and sound
recipes — no developer knowledge needed. **[Read the
manual](docs/manual/boreas-manual.pdf).** It's bundled in every
[release](../../releases/latest) as `boreas-manual.pdf`, and on a MOD device it's the
*documentation* button in the plugin's info dialog.

## Building & contributing

Building from source, cross-compiling for the MOD Dwarf, the DSP internals and code
architecture, and the release / publishing workflow all live in
**[DEVELOPERS.md](DEVELOPERS.md)**.

## Acknowledgements

- [DISTRHO Plugin Framework (DPF)](https://github.com/DISTRHO/DPF) — the LV2/VST/CLAP framework.
- [MOD Audio](https://mod.audio) — the Dwarf, MOD Desktop, mod-plugin-builder, and the modgui design.
- Inspired by the EHX Deep Freeze and the family of freeze / infinite-sustain pedals.

## License

Plugin code is ISC-licensed. DPF is ISC-licensed (see [`dpf/LICENSE`](dpf/LICENSE)).
