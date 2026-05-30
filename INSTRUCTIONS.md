# Instructions for an LLM continuing this work

You are working in a **MOD plugin template repository**. Your job is
typically one of:

1. **Rename the template** to a fresh plugin name for the user.
2. **Implement DSP** (replace `MyPluginPlugin.cpp`'s passthrough with
   real audio processing the user describes).
3. **Add parameters / a modgui layout** that exposes the DSP knobs.
4. **Cross-build and deploy** to a MOD Dwarf the user has connected.
5. **Release** a prebuilt bundle via `make release version=...`.

Most of the build infrastructure is wired up already. Below are the
hard-won gotchas — read them before you touch the build system, the
modgui, or the Dwarf deploy. Several of these will silently produce a
wrong-looking build with no error message if you trip them.

---

## Build system gotchas

### DPF's `BUILD_CXX_FLAGS` is hard-reset
`dpf/Makefile.base.mk` (around line 347) assigns `BUILD_CXX_FLAGS = …`
unconditionally, then appends `$(CXXFLAGS)` at the end. **User-defined
`-D…` macros must go through `CXXFLAGS`, not `BUILD_CXX_FLAGS`.** The
template's inner Makefile already does this for `MYPLUGIN_BETA=1`. If
you add another conditional compile flag, follow the same pattern.

### `lv2_ttl_generator` only works on the host architecture
DPF's TTL generator `dlopen()`s the plugin `.so` to introspect ports
and emit `manifest.ttl` + `<plugin>.ttl`. It cannot introspect a
cross-compiled aarch64 `.so` from an x86_64 host. The Dwarf cross-build
pipeline (`mod-build/build-plugin.sh`) sidesteps this by doing:

1. native (x86_64) build → produces TTL + modgui bundle layout
2. stash the bundle
3. clean + cross-build the `.so` for aarch64
4. overlay the aarch64 `.so` into the stashed bundle

Don't try to merge these steps — the dlopen will fail with a cryptic
error and produce no TTL.

### Plugin unique-id must be unique
`DistrhoPluginInfo.h` declares `DISTRHO_PLUGIN_UNIQUE_ID` as a 4-char
code (`d_cconst` packs four chars into an int32_t). Hosts use it to
distinguish state between plugins. **If you have multiple plugins (or
the BETA variant), each needs a distinct code.** Conflicts manifest as
"my preset loaded into the wrong plugin" or simply two plugins one of
which is invisible.

### `make all` then `make` doesn't re-run TTL generation
DPF's TTL generator is wired to `make ttl` (called by the top-level
`make all`). If you only re-build the `.so` (e.g. `make -C plugins/...`)
the TTL stays stale and the new ports won't show. Always re-run the
top-level `make` after parameter changes.

### Renaming a parameter requires three places to update
1. Inner `MyPluginPlugin.cpp` — the `Parameter.symbol` in `initParameter()`
2. `modgui.ttl` — the `lv2:symbol` for that port
3. `modgui/icon-myplugin.html` — `mod-port-symbol="..."` on the bound
   widget

If any one is out of sync, MOD-UI will log "No such symbol: ..." and
that knob won't bind to the LV2 port.

---

## Dwarf cross-build / deploy gotchas

### Dropbear has no SFTP — use `scp -O`
The Dwarf runs Dropbear SSH. Modern OpenSSH `scp` (≥ 9.0) defaults to
the SFTP subsystem and fails with "subsystem request failed on channel
0". The `-O` flag forces legacy scp protocol. The template's
`dwarf-deploy` target already does this.

### Two independent lilv plugin caches on the Dwarf
After deploy you must restart BOTH `jack2` AND `mod-ui`. They keep
separate cached views of the plugin world:

- `jack2` hosts `mod-host` internally (appears as `mod-jackd` in logs)
  and caches the plugin world at startup. Without a restart the
  pedalboard fails with "can't get plugin" / "Error adding effect".
- `mod-ui` (the web UI) has its OWN cache for port lists and modgui
  rendering. Without a restart the UI shows the OLD port set and
  reports "No such symbol: …" errors for new ports — even though
  `jack2` picked the new bundle up correctly.

The template's `dwarf-deploy` does `systemctl restart jack2 mod-ui`.

### Plus: hard-refresh the browser
mod-ui's JavaScript front-end caches plugin metadata in the browser.
After a deploy, you need to **hard refresh** the MOD-UI page
(Ctrl-Shift-R) or the user sees the old UI even after the services
restart. The template's deploy step prints a reminder.

### The Dwarf may have two LV2 dirs in scan path
`/root/.lv2/` (per-user, persistent) and sometimes `/usr/lib/lv2/`
(system). If a stale bundle sits in the system path, it can shadow your
fresh `/root/.lv2/` deploy. Have the user SSH in and check:
```
find / -name '<plugin>.so' 2>/dev/null
```
If two paths show up, delete the stale one.

### `build/dwarf/` is the cross-build output, NOT `bin/`
The native host build goes to `bin/<plugin>.lv2/` (x86_64). The Dwarf
cross-build goes to `build/dwarf/<plugin>.lv2/` (aarch64). Don't confuse
the two when packaging release tarballs.

### Container output is owned by root
The cross-build container runs as root; the output bundle would be
unwritable from the host without chown. `build-plugin.sh` chowns
`/out` to `$HOST_UID:$HOST_GID` at the end, which the Makefile passes
in via `-e HOST_UID=$$(id -u) -e HOST_GID=$$(id -g)`. Don't remove this.

---

## DSP patterns worth knowing

When the user describes an effect, these patterns are commonly needed.
**None of them are pre-installed** in this template's
`MyPluginPlugin.cpp` — it's a clean passthrough+gain. Add what you need.

### Equal-power dry/wet crossfade
For a MIX knob (0 = dry, 1 = wet) that keeps output power constant
across the knob:
```cpp
const float mix     = fMix;
const float dryGain = std::cos(mix * kHalfPi);   // kHalfPi = π/2
const float wetGain = std::sin(mix * kHalfPi);
outL[f] = dryGain * dry + wetGain * wet;
```
Linear `(1-mix) * dry + mix * wet` summing two full-amplitude signals
boosts ~6 dB at MIX = 0.5 for decorrelated signals.

### Output trim knob in dB
Standard `±12 dB` post-mix trim, applied as `10^(level/20)`:
```cpp
const float levelLin = std::pow(10.0f, fLevelDb * (1.0f / 20.0f));
outL[f] *= levelLin;
```

### Equal-power pan
For mapping a position `pos ∈ [-1, +1]` to L/R gains with constant power:
```cpp
const float theta = (pos + 1.0f) * 0.5f * kHalfPi;   // [0, π/2]
const float panL  = std::cos(theta);
const float panR  = std::sin(theta);
```

### Per-string / per-comb saturation
For high-Q resonators (Karplus-Strong, comb filters with feedback near
1) a tanh saturator inside the feedback loop bounds the stored energy
at ±1 regardless of feedback gain — models the physical limit of a
real string (bridge slap / friction). Linear at typical play levels:
```cpp
const float fbSample = std::tanh(fLastOut * fFeedback);   // not just fLastOut * fFeedback
```

### Noise gate (peak envelope + smoother)
```cpp
const float xAbs = std::fabs(x);
if (xAbs > fEnv) fEnv = xAbs;                              // instant attack
else             fEnv = xAbs + (fEnv - xAbs) * fReleaseCoef; // ~80 ms release
const float target = (fEnv > thresholdLin) ? 1.0f : 0.0f;
fGateOpen += (target - fGateOpen) * fSmoothCoef;           // ~8 ms transition
```
Coefficients: `releaseCoef = exp(-1 / (fs * 0.080))`, `smoothCoef =
1 - exp(-1 / (fs * 0.008))`.

### DC blocker (one-pole HPF)
```cpp
y = x - prevX + alpha * prevY;     // alpha = 0.996 → fc ≈ 30 Hz at 48k
prevX = x;
prevY = y;
```

### Parameter smoothing (zipper-noise avoidance)
Knob turns and DAW automation change parameters at block boundaries.
For audio-rate-sensitive params (gain, mix, level), interpolate from
old to new across the block:
```cpp
// Per-sample linear ramp
fGain += (fGainTarget - fGain) * fGainSmoothCoef;
// Or per-block linear interp:
const float gainStep = (fGainTarget - fGainCurrent) / frames;
```
Without this you'll get audible clicks on fast knob turns.

### Denormal protection
Feedback loops at long decay produce denormalized floats that are
~100× slower on x86. Add at the top of `activate()` if you have
high-Q feedback:
```cpp
#include <xmmintrin.h>
_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
_MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
```
Or sprinkle a tiny DC offset (`1e-25f`) into feedback paths.

---

## modgui patterns

### Pedal dimensions
Standard MOD pedals are 640 px wide; the height depends on knob count.
This template uses a smaller 320×160 frame to keep the one-knob example
visually proportional — scale up when you add more controls. Pedals
wider than ~700 px scroll horizontally in the pedalboard view.

### Required structural elements
MOD-UI complains visually (broken pedal frame) if these are missing
from `icon-*.html`:
- `<div mod-role="drag-handle">` covering the whole pedal
- `<div mod-role="bypass-light">` for the LED
- `<div mod-role="bypass">` for the footswitch
- The audio I/O `{{#effect.ports.audio.input}}…{{/…}}` loops

### LV2 enum parameters → custom dropdowns
MOD-UI's default film-strip widget renders enums (e.g. a multi-value
selector with named values) badly. The clean pattern is a custom
`<select>` in `icon-*.html` bound via a custom `mod-role`, then in the
JS `start` handler, translate the dropdown's string value to the enum
integer and write it via `funcs.set_port_value`:
```js
const VALUES = ['option-a', 'option-b', 'option-c'];   // matches C++ enum order
$select.on('change', function() {
    funcs.set_port_value('my_enum', VALUES.indexOf(this.value));
});
```
And in the `change` event branch, do the inverse to keep the
`<select>` in sync with preset recall / automation:
```js
$select.val(VALUES[Math.round(event.value)]);
```
Keep a guard flag in `icon.data('suppress-emit', ...)` so updating the
`<select>` programmatically doesn't fire a feedback `set_port_value`.

### Default vs custom knob sprite
The template uses MOD-UI's built-in knob sprites — pick a palette via
`modgui:knob "gold"` (or "yellow", "red", etc.) in `modgui.ttl`. The
stylesheet deliberately does NOT override `.mod-knob-image`'s
background, so MOD-UI applies its own knob asset based on the palette
name. To switch to a custom knob, drop a horizontal film-strip PNG at
`modgui/knobs/<name>.png` and add a CSS rule:
```css
.myplugin-pedal .mod-knob-image {
    background-image: url(/resources/knobs/<name>.png{{{ns}}});
    background-repeat: no-repeat;
    background-position: left center;
}
```

### `/resources/...{{{ns}}}` cache-busting
Static modgui resources are served by MOD-UI's `EffectResource`
handler. The `{{{ns}}}` Mustache token expands to a query string
scoping the request to your plugin's modgui directory and busting the
browser cache when the plugin version changes. Keep it on every
resource URL in the CSS.

### Screenshots
MOD-UI generates pedalboard preview thumbnails on demand once the
plugin is loaded; you usually don't need to ship a `screenshot-*.png`
during development. When ready to publish, take a clean screenshot from
inside MOD Desktop / MOD-UI itself and reference it from `modgui.ttl`
via `modgui:screenshot` + `modgui:thumbnail`.

---

## Customising the modgui look

The template ships with MOD-UI's default skin for the frame, brand
text, bypass light, footswitch, and knob — `stylesheet-myplugin.css`
only POSITIONS those elements; MOD-UI provides their visuals based on
the `modgui:model` / `modgui:panel` / `modgui:color` / `modgui:knob`
strings in `modgui.ttl`. To go custom, override the relevant rule.

**Custom pedal frame:**
```css
.myplugin-pedal {
    border-radius: 14px;
    background:
        radial-gradient(ellipse at 20% 0%, rgba(255, 200, 120, 0.18), transparent 60%),
        linear-gradient(135deg, #3a1f12 0%, #5a3220 45%, #2c170c 100%);
    box-shadow:
        inset 0 0 0 2px rgba(255, 200, 130, 0.18),
        inset 0 0 32px rgba(0, 0, 0, 0.55),
        0 6px 14px rgba(0, 0, 0, 0.55);
    color: #f4e3c2;
    font-family: "nexa", "Lato", "Helvetica Neue", Helvetica, Arial, sans-serif;
}
```

**Custom brand / label text:**
```css
.myplugin-pedal .mod-plugin-brand h1 {
    margin: 0;
    font-size: 11px;
    letter-spacing: 0.3em;
    text-transform: uppercase;
    color: rgba(245, 217, 154, 0.7);
}
.myplugin-pedal .mod-plugin-name h1 {
    margin: 0;
    font-size: 20px;
    letter-spacing: 0.18em;
    text-transform: uppercase;
    color: #f5d99a;
    font-weight: 700;
    text-shadow: 0 1px 1px rgba(0, 0, 0, 0.6);
}
```

**Custom knob sprite** (horizontal film-strip PNG, frames are square):
1. Drop the file at `modgui/knobs/<name>.png`.
2. Add to the Makefile copy rule if it isn't already (it copies the
   whole `knobs/` directory by default).
3. Override the background image in CSS:
   ```css
   .myplugin-pedal .mod-knob-image {
       background-image: url(/resources/knobs/<name>.png{{{ns}}});
       background-repeat: no-repeat;
       background-position: left center;
   }
   ```
The `{{{ns}}}` Mustache token is a cache-busting query string MOD-UI
appends; keep it in every resource URL.

**Custom bypass light + footswitch** (drawn entirely in CSS):
```css
.myplugin-pedal .mod-light {
    width: 14px; height: 14px;
    border-radius: 50%;
    background: #401a09;
    border: 1px solid rgba(0, 0, 0, 0.7);
    box-shadow: inset 0 1px 2px rgba(0, 0, 0, 0.6);
}
.myplugin-pedal .mod-light.on {
    background: radial-gradient(circle at 30% 30%, #ffd55a, #d27a14 70%, #6e3a04);
    box-shadow:
        inset 0 1px 2px rgba(0, 0, 0, 0.3),
        0 0 8px rgba(255, 180, 60, 0.7);
}
.myplugin-pedal .mod-footswitch {
    width: 24px; height: 24px;
    border-radius: 50%;
    background: linear-gradient(180deg, #4a2a18 0%, #281407 100%);
    border: 1px solid rgba(0, 0, 0, 0.7);
    box-shadow:
        inset 0 1px 2px rgba(255, 200, 130, 0.25),
        inset 0 -2px 4px rgba(0, 0, 0, 0.6),
        0 2px 3px rgba(0, 0, 0, 0.6);
}
```

**Pedal size:** the template is 320×160. For multi-knob layouts go wider
(typical MOD pedal is 640×320 for 5-9 knobs). Wider than ~700 px scrolls
horizontally in the pedalboard view.

---

## Workflow when the user gives you a task

1. **Read INSTRUCTIONS.md** (this file) and **`Makefile`** before touching
   the build system or modgui. The patterns and gotchas above will
   save you reverse-engineering them.
2. **For DSP work:** edit `plugins/MyPlugin/MyPluginPlugin.cpp`. Add
   parameters via the `ParamIndex` enum and `initParameter()`. Echo
   them in `getParameterValue()` / `setParameterValue()`. Update
   `modgui.ttl` and the icon HTML with the new port symbols.
3. **For modgui work:** edit the three files in `modgui/`. Drop a PNG
   into `modgui/knobs/` and override `.mod-knob-image` in the CSS to
   replace the default MOD knob sprite.
4. **Build + test on desktop first:** `make && ./install.sh`. Restart
   MOD Desktop. Drag the plugin onto a pedalboard.
5. **Then test on Dwarf if applicable:** `make dwarf`. Hard-refresh the
   MOD-UI browser tab. Drag the plugin onto a pedalboard.
6. **Release when stable:** `make release version=x.y.z`. Refuses on a
   dirty tree and uploads both bundles to a fresh GitHub release.

If something looks broken on the Dwarf but right on desktop, the cause
is almost always one of:
- service caches (didn't restart `jack2` + `mod-ui`)
- browser cache (didn't hard-refresh)
- stale system-path LV2 bundle shadowing the user-path one
- modgui symbol mismatch between `<plugin>.ttl` and `modgui.ttl`
