# RealTimeGUI

A Haiku native app that detects your current audio driver and output
frequency, then helps you tune and apply that driver's real-time playback
buffer settings -- without hand-editing files under
`~/config/settings/kernel/drivers/` yourself.

Built as a from-scratch reuse of [GLToogle](https://github.com/ablyssx74/GLToogle)'s
basic app scaffolding (window/layout structure, update-checker thread,
package templates) with entirely new detection/settings logic.

### To build
```
make release
```

## What it actually does

1. **Detects your active audio driver** by scanning `/dev/audio/hmulti/`
   for published device subdirectories -- the same place every Haiku audio
   driver publishes itself, confirmed by reading each driver's own
   `publish_devices()`/`make_device_names()` source in
   [haiku/haiku](https://github.com/haiku/haiku/tree/master/src/add-ons/kernel/drivers/audio)
   (not assumed by pattern-matching driver names, which turned out to be
   unreliable -- see below).
2. **Detects your current output frequency** via `BMediaRoster`'s public
   API (`GetAudioOutput()` for the physical sink node, then
   `GetAllOutputsFor()`/`GetFormatFor()` for its live negotiated format) --
   the same frequency Haiku's own Media preferences "Frequency" control
   sets.
3. **Looks up whether that driver has a real-time-tunable settings file**
   in a small built-in catalog (see table below). If it doesn't, the app
   says exactly that: `Realtime settings not available.`
4. **Recommends a starting buffer size/count** for your current frequency,
   explains the reasoning, and lets you adjust both values manually before
   applying.
5. **Applies your choice** by updating (or creating) that driver's own
   settings file under `~/config/settings/kernel/drivers/` -- editing only
   the relevant key(s) in place, line by line, leaving everything else in
   the file (comments, other keys, a driver's or your own prior
   customization) untouched.

## Why this needed real research, not assumptions

Haiku's audio drivers are **not** standardized on one settings-file
convention. Every driver picks its own filename and its own key names,
independently -- confirmed by reading each driver's own source and shipped
`.settings` file directly, not by assuming one driver's convention applies
to the rest. Two examples of why that assumption would have broken things:

- `hda` uses `play_buffer_frames`/`play_buffer_count`/`record_buffer_frames`/`record_buffer_count`.
  `auich`/`es1370`/`echo`/`emuxki` use plain `buffer_frames`/`buffer_count`
  instead. `ice1712` uses a single `buffer_size` key with no count key at
  all.
- `usb_audio`'s own devfs publish path is `/dev/audio/hmulti/usb/`, **not**
  `/dev/audio/hmulti/usb_audio/`, even though its own `DRIVER_NAME` macro
  is `"usb_audio"` -- the two don't match for this driver, so this app
  never assumes a driver's internal name equals its devfs path segment.

## Driver catalog

| Driver | devfs segment | Settings file | Tunable? | Keys |
|---|---|---|---|---|
| Intel HD Audio | `hda` | `hda.settings` | Yes | `play_buffer_frames`, `play_buffer_count` (+ `record_*`, not written by this app) |
| Intel AC'97 | `auich` | `auich.settings` | Yes | `buffer_frames`, `buffer_count` |
| Ensoniq ES1370 | `es1370` | `es1370.settings` | Yes | `buffer_frames`, `buffer_count` |
| Echo Digital Audio | `echo` | `echo.settings` | Yes | `buffer_frames`, `buffer_count` |
| Sound Blaster Live!/Audigy | `emuxki` | `emuxki.settings` | Yes | `buffer_frames`, `buffer_count` |
| VIA Envy24/ICE1712 | `ice1712` | `ice1712.settings` | Yes | `buffer_size` only |
| SiS 7018 | `sis7018` | `sis7018` (renamed, no `.settings` suffix) | No | logging/tracing only |
| USB Audio Class | `usb` | `usb_audio.settings` | No | logging/tracing only; buffer size (2048 samples / 2 sub-buffers) is hardcoded in the driver itself |
| VIA VT82xx AC'97 | `auvia` | none | No | -- |
| AMD Geode | `geode` | none | No | -- |
| Sound Blaster 16 | `sb16` | none | No | -- |
| VirtIO Sound (VMs) | `virtio` | none | No | -- |
| Null Audio | `null` | none | No | placeholder device |

Every row above was confirmed by reading that driver's own directory in
`haiku/haiku`'s source tree directly -- not guessed. `cmedia` is
deliberately **not** in the table: it's confirmed to ship no `.settings`
file either way, but its own devfs segment name wasn't independently
traced through its publish path the way every other row above was, so it's
left out rather than assert a label/path that might be wrong. The `echo`
driver's own hardware sub-variants (its `24`/`3g`/`gals`/`indigo`
subdirectories) are assumed to share `echo`'s own settings file and devfs
segment, not checked individually.

If your device shows up as a driver not in this table, the app says so
honestly rather than guessing -- with a pointer to check
`haiku/haiku`'s own source for a matching `.settings` file if you want to
try tuning it by hand.

## The recommendation

Heuristic, not vendor-authoritative. It targets roughly the same
per-buffer duration (~2.7ms) as a real-world-confirmed **128 frames / 4
buffers @ 48000Hz** setup this logic was built around, scaled linearly to
whatever frequency is actually detected, and snapped to the nearest buffer
size actually seen across the driver settings examples gathered for the
catalog above (64/128/256/512/1024/2048).

This is a starting point, not a guarantee -- actual real-time headroom
depends on the specific hardware and how much other work (effects
processing, etc.) is competing for the same CPU core. If clicks/pops show
up at the recommended setting, raising `buffer_count` first is cheaper in
added latency than raising `buffer_frames`.

`hda.settings`'s own shipped comment gives an independent, official
example worth knowing: *"latency will probably be at least
2\*buffer_frames/sample_rate... For real time audio, a buffer of 1024
frames at 192000 should be good (<15ms)."* That's a more conservative
(larger-buffer) target than this app's own ~2.7ms-per-buffer heuristic --
the app's target prioritizes the same low-latency ratio as the
real-world-confirmed 48kHz setup it was built around; HDA's own comment
prioritizes safety headroom at a much higher sample rate. Both are shown
in-app so you can judge for yourself.

## Applying a setting

Writes are line-oriented and conservative: an existing key (commented out
or not) has its value replaced in place; a key that isn't present yet is
appended. Everything else already in the file is left untouched. A file
that doesn't exist yet is created with a short header noting it was
written by this app.

**Restart Media Services (or reboot) for a new setting to actually take
effect** -- Haiku's kernel drivers read their settings file at driver load
time, not continuously.

## Known limitations, honestly

- Only `play_buffer_frames`/`buffer_frames`/`buffer_size`-style keys are
  exposed and written. `hda`'s own separate `record_buffer_frames`/`record_buffer_count`
  keys exist but aren't touched by this version -- out of scope for what
  was actually asked for (real-time *monitoring* latency, i.e. playback).
- If more than one audio device is active at once (e.g. an HDMI output
  alongside a real audio interface), this version shows and lets you tune
  only the first *tunable* one found, not a picker across all of them.
- `auich`/`es1370`/`echo`/`emuxki`'s own settings files also expose a
  `sample_rate` override key. This app deliberately never touches it --
  your system's own Media preferences already control the sample rate,
  and overriding it separately in the driver's own settings risked
  creating a mismatch instead of fixing anything.
- The recommendation heuristic itself (see above) is unconfirmed pending
  real-world testing across more than the one 48kHz setup it was built
  around.
