# Boreas — Freeze Pedal (LV2 / MOD) — Design Spec

**Date:** 2026-05-30
**Status:** Approved for implementation planning
**Target:** MOD Desktop + MOD Dwarf (LV2), built on the DISTRHO Plugin Framework (DPF) template in this repo.

---

## 1. Goal

A "freeze / sound retainer" pedal that captures a short slice of live audio and loops it
into a seamless, infinite drone. Behaviourally modelled on the **Electro-Harmonix Deep
Freeze** (NYC DSP), reproducing its **Moment** and **Latch** modes, **layering** (chord
building), and **gliss** (portamento-style morph between freezes) — **excluding Auto mode**,
which is explicitly out of scope.

The granular freeze itself uses continuous background recording, a copied snapshot, and two
overlapping Hann-windowed playheads so the loop has no clicks and no tremolo.

### What we replicate vs. the real pedal

| Deep Freeze feature | Boreas | Notes |
|---|---|---|
| Seamless infinite freeze | ✅ | dual-Hann 50%-overlap granular |
| Moment mode (hold to freeze) | ✅ | |
| Latch mode (press to freeze, sustains) | ✅ | |
| Layering / chord building | ✅ | `LAYER` retains prior freeze when a new one is captured |
| Gliss (variable morph time) | ✅ | `GLISS` knob, CCW = instant |
| Independent Dry / Effect volumes | ✅ | linear, not a crossfade |
| Auto mode | ❌ | out of scope (user decision) |
| Single footswitch + double-tap-to-off | ❌ → improved | replaced by a dedicated **Clear** switch (see §6) |
| Bypass topology select (analog/digital/hybrid) | ❌ | hardware-only; MOD host handles bypass |

### Deliberate deviations from the original `mod_dwarf_freeze_plugin_plan.md`

1. **Snapshot is copied, not referenced** into the live ring (correctness — see §5.4).
2. **Capture "now"**, not 150 ms in the past — the manual says it freezes *"the sound present
   at the Input jack."* A small transient-safety lookback remains as a tunable (§7).
3. **Double-tap dropped** in favour of a dedicated **Clear** switch (no timing ambiguity).
4. **SPEED/LAYER split into two separate knobs** (the hardware multiplexes one knob only
   because of physical knob count; software has no such constraint).
5. **GLISS added** as a variable knob (the plan had only a fixed crossfade).
6. **Mono in → mono out** (the template is mono → stereo; no reason to duplicate).

---

## 2. Plugin identity & build changes

Rename the template `myplugin` → `boreas` everywhere it is the source of truth:

- `plugins/MyPlugin/` → `plugins/Boreas/`; `MyPluginPlugin.cpp` → `BoreasPlugin.cpp`.
- `plugins/Boreas/DistrhoPluginInfo.h`:
  - brand/name/URI/CLAP-id → `boreas`.
  - `DISTRHO_PLUGIN_NUM_INPUTS  1`, `DISTRHO_PLUGIN_NUM_OUTPUTS 1` (mono → mono).
  - unique IDs: stable `dBor` / brand `Bore`; beta `dBoB` / brand `BorB`.
  - beta macro `MYPLUGIN_BETA` → `BOREAS_BETA`.
  - keep `DISTRHO_PLUGIN_IS_RT_SAFE 1`, `WANT_PARAMETER_VALUE_CHANGE_REQUEST 1`.
- `plugins/Boreas/Makefile`: `NAME = boreas`, `FILES_DSP = BoreasPlugin.cpp`, beta macro rename.
- Top-level `Makefile`: `PLUGIN := boreas`, `PLUGIN_DIR := plugins/Boreas`, `BRAND := boreas`,
  `LABEL := Boreas`, `PLUGIN_URI_BASE := http://boreas.local/plugins`.
- modgui files renamed `*-boreas.{html,css,js}`; `modgui.ttl` URIs/brand/label updated.

The DSP utility classes (`CircularBuffer`, `EnvelopeGenerator`, `GrainPlayer`, freeze engine)
live in their own headers under `plugins/Boreas/` and **do not depend on any DPF type** — they
take plain `float`/`int` and operate on caller-owned buffers, so they can be unit-tested with a
plain `g++` harness (§10).

---

## 3. Parameters (LV2 ports)

All control-input ports. Edge-triggered switches are read once per `run()` block (block-rate is
fine; MOD blocks are ~1–5 ms, far below any human timing).

| # | Symbol | Name | Type / hints | Range | Default | Meaning |
|---|---|---|---|---|---|---|
| 0 | `footswitch` | Freeze | bool, automatable | 0 / 1 | 0 | Capture / freeze. Momentary on hardware. |
| 1 | `clear` | Clear | bool, automatable | 0 / 1 | 0 | Release the freeze (fade out over SPEED, then off). Driven as a trigger (1→0) by GUI/hardware; the DSP acts on the rising edge. |
| 2 | `mode` | Mode | int enum, automatable | 0 = Moment, 1 = Latch | 1 | Footswitch behaviour. |
| 3 | `speed` | Speed | float, automatable | 0.0 – 1.0 | 0.2 | Master envelope fade in/out time. |
| 4 | `layer` | Layer | float, automatable | 0.0 – 1.0 | 0.0 | Retained volume of prior freeze on a new capture (Latch). |
| 5 | `gliss` | Gliss | float, automatable | 0.0 – 1.0 | 0.0 | Morph time between successive freezes. CCW (0) = instant. |
| 6 | `dry_vol` | Dry | float, automatable | 0.0 – 1.0 | 0.5 | Dry output level. |
| 7 | `effect_vol` | Effect | float, automatable | 0.0 – 1.0 | 0.5 | Frozen output level. |

**Symbol sync (per INSTRUCTIONS.md — three places must agree):** each `parameter.symbol` in
`initParameter()` ↔ `lv2:symbol` in `modgui.ttl` ↔ `mod-port-symbol` in `icon-boreas.html`.
A mismatch shows "No such symbol: …" in MOD-UI.

---

## 4. Control semantics (per mode)

Three time/amount controls are cleanly separated (the hardware overloads SPEED/LAYER; we don't):

- **SPEED** — master envelope. Fade-in when the effect first engages from silence; fade-out
  when **Clear** is pressed; and in **Moment** mode the fade on hold / release. Polarity:
  `speed = 0.0` → slow swell (~4 s), `speed = 1.0` → fast (~5 ms). (See `kSpeedMin/MaxSec`, §7.)
- **GLISS** — morph time when one freeze transitions into the next while already engaged.
  `gliss = 0.0` → instant (off); `gliss = 1.0` → ~2 s. Only meaningful in Latch (Moment holds a
  single freeze at a time). (See `kGlissMaxSec`, §7.)
- **LAYER** — sampled **at the instant of a Freeze press**: the volume the *existing* freeze is
  retained at when the new grain is folded in. `0.0` = replace (no layering); `1.0` = previous
  layers kept at full volume (full chord stack). Linear.

### Moment mode (`mode = 0`)
- Freeze rising edge (0→1): capture current input window; master envelope → Attack (SPEED).
- Freeze falling edge (1→0): master envelope → Release (SPEED); when it reaches 0, idle.
- LAYER / GLISS inactive (a hold is a single freeze).

### Latch mode (`mode = 1`)
- **First** Freeze press while OFF: capture; master envelope → Attack (SPEED).
- **Subsequent** Freeze press while ON: capture a new grain into `incoming`; run a GLISS morph
  (§5.5) that blends to `combined·LAYER + incoming`. Master envelope stays at 1.
- **Clear** press: master envelope → Release (SPEED); when it reaches 0, clear `combined` → OFF.
- Mode is read at press time; switching mode mid-freeze does not disturb the current sound.

---

## 5. DSP architecture

### 5.1 Overview of `run()` (per block, then per sample where noted)
1. Read control ports; detect Freeze rising/falling and Clear rising edges (block-rate).
2. Dispatch the state machine (§4) → may trigger capture, gliss, attack, release.
3. Per sample:
   a. Write `in[f]` into the circular buffer; advance write index (always, unconditionally).
   b. If the freeze engine is active, produce `wet` (§5.3–5.5) and multiply by the master
      envelope value.
   c. `out[f] = dry_vol·in[f] + effect_vol·wet` (vols per-sample smoothed, §5.6).

### 5.2 `CircularBuffer`
- Mono ring sized for **≥ 1 s at the max supported sample rate**. Allocate for 96 kHz
  (`kMaxSampleRate`) so `run()` never allocates and `sampleRateChanged` never reallocates.
- Cleared to zero in `activate()` (an early freeze yields silence, not a click/garbage).
- `write(x)` each sample; `read(indexFromWrite)` for capture. **Per-instance member**, never
  global/static (a static ring would bleed across plugin instances).

### 5.3 `GrainPlayer` (dual-Hann freeze loop)
- Owns a **copied** snapshot buffer of length `W` (the window, §7). Two playheads A and B with
  phases offset by `W/2`. `W` is rounded to an **even** sample count so `W/2` is exact.
- Output per sample: `snap[a]·hann[a] + snap[b]·hann[b]`, `b = (a + W/2) mod W`, then advance
  both phases mod `W`.
- `hann[]` is a precomputed LUT of length `W`, `hann[n] = 0.5 − 0.5·cos(2π·n/W)`, rebuilt on
  `sampleRateChanged`. At 50 % overlap, `hann[n] + hann[n+W/2] = 1.0` exactly → constant
  amplitude, no tremolo; the loop seam (phase 0, weight 0) is masked while the other playhead is
  mid-grain. This is the click-free guarantee.

### 5.4 Snapshot capture (the correctness fix)
- On a Freeze press, **copy** the most recent `W` samples (offset back by `kLookbackSamples`,
  §7) out of the ring into a stable snapshot buffer. This is a bounded `O(W)` copy on a
  footswitch event — RT-safe (no alloc, no lock; buffers pre-sized).
- The snapshot must be a copy, **not** a pair of indices into the live ring: recording continues
  unconditionally, so indices into the ring would be overwritten within ≤ 1 s and the "infinite"
  drone would be destroyed.

### 5.5 Freeze engine: layering + gliss
Buffers (all length `W`, pre-allocated): `combined` (the frozen chord currently looping) and
`incoming` (a freshly captured grain during a morph). Two `GrainPlayer`s read them.

- **Off → first freeze:** copy capture into `combined`; master env attacks.
- **Re-freeze (Latch, already on):** copy capture into `incoming`. Start a gliss of duration
  `G` samples (from `gliss`). Per sample over `t: 0→1`:
  - `oldGain = lerp(1, LAYER, t)` — existing chord fades toward its retained level.
  - `newGain = lerp(0, 1, t)` — new grain fades in.
  - `wet = combinedPlayer()·oldGain + incomingPlayer()·newGain`.
  - At `t == 1`: fold `combined[i] = combined[i]·LAYER + incoming[i]` (bounded `O(W)`), then
    resume a single player on `combined`. If `G == 0` (gliss off), fold immediately.
- **Level safety:** with `LAYER → 1` and repeated stacks, `combined` can exceed ±1. Apply a
  gentle `tanh` soft-limit when folding (`combined[i] = tanh(combined[i])`), transparent at
  sane levels, graceful when pushed. (Tunable; see INSTRUCTIONS' tanh note.)

### 5.6 `EnvelopeGenerator` (master AR)
- States Idle / Attack / Sustain / Release; per-sample value in [0, 1] multiplies the whole wet.
- Attack and Release time both derived from `speed` (symmetric), exponential mapping between
  `kSpeedMinSec` and `kSpeedMaxSec` (§7). Coefficients recomputed when `speed` changes or on
  `sampleRateChanged`.

### 5.7 Parameter smoothing
- `dry_vol`, `effect_vol`: per-sample one-pole smoothing toward target to avoid zipper noise on
  knob turns / automation (INSTRUCTIONS pattern).
- `speed`, `gliss`, `layer`: affect coefficients / are sampled at events, not per-sample audio
  multipliers → no smoothing needed.

### 5.8 RT-safety
- All buffers allocated in the constructor / `activate` / `sampleRateChanged` (non-RT contexts).
- `run()` performs only bounded arithmetic and bounded `O(W)` copies on switch events. No
  allocation, locking, or I/O. Honours `DISTRHO_PLUGIN_IS_RT_SAFE 1`.

---

## 6. Footswitch / control scheme

The Deep Freeze uses one footswitch with double-tap-to-off; we use **two boolean ports** so
every action is an unambiguous edge and no timing window is needed:

- **`footswitch` (Freeze):** rising edge captures. In Moment, the falling edge releases.
- **`clear` (Clear):** rising edge releases the freeze (fade out over SPEED, then off).

On the **MOD Dwarf**, the user addresses each port to a physical footswitch. The Freeze switch
**must be addressed as a momentary actuator** so the DSP sees true press/release edges — the
`mode` parameter (Moment/Latch) provides the musical behaviour, not the MOD actuator config.
(If Freeze were addressed as a toggle, Moment mode would never see a release edge.)

The plugin's overall bypass remains the host/MOD bypass (separate concern); Clear is a musical
"release", not a bypass.

---

## 7. Key constants & tunables

Centralised named constants (single place to tune; some intended for future promotion to ports):

| Constant | Default | Purpose |
|---|---|---|
| `kMaxSampleRate` | 96000 | Buffer sizing ceiling (alloc once). |
| `kRingSeconds` | 1.0 | Circular buffer length. |
| `kWindowSec` | 0.15 | Grain window `W` length (rounded to even samples per fs). |
| `kLookbackSamples` | small (≈ 0–30 ms; default 0) | How far behind the write head to start the capture, for transient safety. **Factored as a single constant and intended to be promoted to an LV2 port later** so the user can A/B test lookback on hardware without a code change. |
| `kSpeedMinSec` / `kSpeedMaxSec` | 0.005 / 4.0 | Master env time range (`speed=1`→min, `speed=0`→max). |
| `kGlissMaxSec` | ~2.0 | Gliss morph time at `gliss=1` (`gliss=0`→0). |

> **Future work hook (per user):** promoting `kLookbackSamples` to a `lookback` LV2 port is a
> deliberate near-term experiment. Keep the capture offset read from one place so the change is
> a parameter wire-up, not a DSP rewrite.

---

## 8. modgui

- Widen the pedal frame from the template's 320×160 to a multi-knob layout (≈ 640 px wide) for
  five knobs + a Mode dropdown + the switch furniture.
- Knobs: **DRY, EFFECT, SPEED, LAYER, GLISS** (`input-control-port`, bound by `mod-port-symbol`).
- **Mode** as a custom `<select>` (Moment / Latch) — the LV2-enum-to-dropdown pattern in
  INSTRUCTIONS: translate string ↔ integer in `script-boreas.js`, with the `suppress-emit`
  guard so programmatic sync doesn't echo a `set_port_value`.
- **Freeze / Clear**: rendered as buttons. Freeze sends `1` on mouse-down and `0` on mouse-up
  (momentary), Clear sends a `1`→`0` trigger (INSTRUCTIONS' one-shot button pattern). Primary
  control on the Dwarf is via hardware footswitch addressing; the GUI buttons are for desktop /
  testing.
- Keep required structural elements: `drag-handle`, `bypass-light`, `bypass`, and the audio I/O
  `{{#effect.ports.audio.*}}` loops.

---

## 9. Build / deploy notes (from INSTRUCTIONS.md)

- After any parameter change, re-run the **top-level** `make` so DPF regenerates the TTL
  (`make -C plugins/...` alone leaves stale TTL → new ports won't show).
- Unique IDs must differ between stable and beta (§2).
- Desktop first: `make && ./install.sh`, restart MOD Desktop, drag onto a pedalboard.
- Dwarf: `make dwarf` (native build → TTL/modgui, then cross-build `.so` overlaid — don't merge
  those steps); deploy restarts `jack2` + `mod-ui`; then **hard-refresh** the browser
  (Ctrl-Shift-R). `scp -O` is already handled. Watch for a stale system-path bundle shadowing
  `/root/.lv2/`.

---

## 10. Testing strategy (TDD for the DSP core)

The pure DSP classes are DPF-independent headers, so write tests **first** and run them natively
with a plain `g++ -std=c++17` harness (no DPF, no host):

- `CircularBuffer`: write N samples, assert ring wrap and `read(offset)` returns expected values.
- `GrainPlayer` / Hann LUT: assert `hann[n] + hann[(n+W/2)%W] ≈ 1.0` for all `n` (COLA); assert
  output of a constant-DC snapshot is constant (no tremolo); assert no discontinuity at the seam.
- `EnvelopeGenerator`: attack ramps monotonically 0→1 and reaches 1; release 1→0; times match
  `speed` mapping within tolerance.
- Freeze engine fold: after a gliss with known `LAYER`, assert `combined == old·LAYER + new`;
  assert `gliss = 0` folds immediately; assert `tanh` safety bounds a stacked overshoot.

Then: build the LV2 bundle, load in MOD Desktop, verify each mode by ear and each knob's effect.
Cross-build + deploy to the Dwarf last; verify footswitch addressing (Freeze = momentary).

---

## 11. Out of scope (this spec)

- **Auto mode** (threshold-triggered freeze) and its Decay/Attack sub-modes.
- Hardware bypass topologies (analog/digital/hybrid) — host concern.
- Stereo widening / decorrelated playheads (mono → mono by decision).
- Pitch-true portamento (GLISS is an amplitude morph/crossfade, matching the pedal's behaviour
  for sample freezes, not a pitch glide).

## 12. Decisions log

- **Name:** Boreas. **Output:** mono → mono. **Scope:** full plan + Deep Freeze layering/gliss,
  minus Auto.
- **Latch off:** dedicated **Clear** switch (not double-tap).
- **Knobs:** SPEED and LAYER as **separate** knobs (+ GLISS).
- **Layering:** included (chord building), `combined = combined·LAYER + incoming`.
- **Lookback:** capture "now" by default; `kLookbackSamples` kept as a single tunable, to be
  promoted to a port for future A/B testing.
