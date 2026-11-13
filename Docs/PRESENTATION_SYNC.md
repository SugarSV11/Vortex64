# Synchronization of the presentation with the monitor

Settings → Graphics → *Vertical synchronization*: **Automatic (recommended)**, **On**, **Off**. Automatic is the default.

## What it is not

The refresh rate of the monitor is **never** the speed of the emulation. The speed of the game is the speed limiter of the core
(wall clock, the VI rate of the game: 60 or 50). A 100, 144 or 165 Hz monitor does not make Mario run faster. Four things are kept apart:

| | who | depends on the monitor? |
|---|---|---|
| speed of the emulation | `CSpeedLimiter` (core) | no |
| frames of the N64 | the game, the RDP | no |
| presentation of a frame | the video plugin: `SDL_GL_SwapWindow` and the swap interval of the context | yes: this is all that changes |
| refresh rate | Qt (`QScreen`) | information |

## How it works

* `Source/Vortex64-gui/PresentSync.{h,cpp}` finds the monitor that holds most of the window of the emulation (the intersection of the
  window with the geometry of each `QScreen`) and reads its `refreshRate()`. A rate outside 20..1000 Hz is *not known* (0): 60 Hz is never
  made up. It follows `QWindow::screenChanged`, `QScreen::refreshRateChanged`, `screenAdded/Removed` and a 250 ms comparison (the window
  moved, the speed changed, a game started). Nothing is sent when nothing changed; the renderer is not made again.
* It tells the video plugin with the optional export `PJ64SetPresentHint` (`Source/Vortex64-plugin-spec/PresentControl.h`): the mode, the
  refresh rate (mHz), the rate of the game (mHz) and whether the emulation is unthrottled (fast forward, "limit FPS" off). The plugin
  answers what it applied with `PJ64GetPresentStatus`. Both plugins (Glide64 = `Vortex64-video`, and GLideN64) implement the same two
  exports with the same header-only policy, so Auto/On/Off mean the same in both. A plugin without them keeps its own box.
* The policy (`PJ64Present::ResolveSwapInterval`, one pure function, tested):

| mode | swap interval |
|---|---|
| Off | 0, immediate |
| On | 1, wait for the monitor (a monitor slower than the game does slow it down: it is what was asked) |
| Auto, emulation unthrottled | 0 |
| Auto, refresh rate not known | -1 adaptive if the driver has it, else 1 |
| Auto, monitor ≥ 98 % of the game's rate | 1: no tearing; frames are shown at the refreshes (60 fps on 60 Hz 1:1, 30 fps on 60 Hz 2 refreshes each, 60 fps on 100 Hz some frames twice) |
| Auto, monitor slower than the game | -1 adaptive (`EXT_swap_control_tear`) if the driver has it, else 0: waiting for every refresh would slow the game down |

* The interval is applied by the thread that swaps, when the plan changes, when the context is made again (full screen) and checked
  every 120 swaps (a driver may drop it). If the driver refuses an interval, the next one is tried (-1 → 1 → 0) and the status says what
  the context really has.
* The hint is kept by `CPlugins` (`SetPresentHint`), which sends it to the video plugin that is loaded and to every one loaded later, and does not
  call a plugin that is being deleted (the frontend calls from its thread, the emulation thread replaces plugins). Inside the plugin the hint is
  one piece under a mutex, taken once per swap. A context is recognised by a counter, not by its address.
* The old boxes "Vertical sync" of the plugins do nothing once Vortex64 manages the presentation; they are shown disabled.

## Decisions

* **No extra buffering.** Only swap intervals the context really accepts are used: 0, 1 and -1 (adaptive). No frame queue, no present
  thread: the latency is that of the swap interval itself (at most one refresh with interval 1).
* **No interpolation, no frame generation.** Frames are repeated as the monitor needs them.
* **VRR / Adaptive Sync / FreeSync.** Nothing is forced. On X11 and XWayland the driver and the compositor decide (a game in full screen on a
  VRR monitor is presented when the swap happens); there is no portable API to ask for it and none of the proprietary ones is used. In Auto
  the swap interval is 1 (or -1 where the monitor is slower than the game): both work with and without VRR. Without VRR everything works as
  described above.
* **XWayland / X11.** The refresh rate is what Qt reports for the screen (RandR). If it cannot be known, Auto does not guess: adaptive (a slow monitor cannot slow the game), else interval 1.

## Maximum FPS (Settings → Graphics)

*Automatic* (default), 20, 30, 50, 60, 75, 90, 100, 120, 144, 165, 240 or *Custom…* (10–1000; the box rejects 0, negatives, text and
anything out of range, and a value out of range in the settings file is read as Automatic). It is the `UserInterface_MaxFps` setting and
travels in the same hint as the vertical sync (`MaxMilliFps`), so there is one system, not two.

* **It limits the presentation, not the game.** A frame that the plugin finishes *before its turn* is not swapped (the screen keeps the
  previous one). Nothing sleeps or spins in the thread of the game: the emulation goes at the speed of the core's limiter, the audio is
  produced at the same rate, no frame is invented or interpolated. A 30 FPS game with a maximum of 144 stays at 30.
* **Pacing.** The turns are deadlines of a monotonic clock one period apart (`PJ64Present::FrameCap`): a frame up to a quarter of a period
  early is admitted (jitter must not halve a game that runs at the limit), the next deadline is the previous plus the period (no drift), and
  after a stall (pause, long frame) the grid restarts from now (no burst).
* **With the vertical sync.** The swap interval is decided as before and does not depend on the limit. With VSync On the monitor also
  limits: 144 on a 100 Hz monitor is about 100.
* **Fast forward.** While the emulation is unthrottled (fast forward, "limit FPS" off) the limit is not applied, as Auto does not wait for the
  monitor either: the speed-up must not be slowed down by a presentation rule.
* **Limit of the approach.** Glide64 reads the *front* buffer in a few frame-buffer effects: with a limit lower than the game's rate that
  buffer is the last frame presented, not the last drawn.
* `VORTEX_PRESENT_TRACE=1` adds `maximum FPS`, `swap interval requested/active`, the presented/s and the held-back/s to the hint and status lines.

## Diagnosis and tests

* `VORTEX_PRESENT_TRACE=1`: prints the monitor and its rate when it changes, every hint sent (mode, monitor, game, throttled), what each
  plugin applies (wanted / applied interval and the reason, and if the driver refused it) and, every 5 s, the status with the VI/s of the
  emulator. It prints nothing per frame.
* `VORTEX_FAKE_REFRESH=<Hz>`: replaces the rate read from Qt (the tests, or to see what Auto does on a monitor that is not at hand).
* `present-policy-test` (offscreen): policy tables, the speed limiter at every refresh rate, what is read of a screen.
* `present-sync-test <rom> <plugin>` (Xvfb + window manager, both plugins): 60/75/100/120/144/165/240 Hz × Auto/On/Off — the hint arrives,
  the planned interval is applied, the VI/s of the emulator never exceed the game's; fast forward; a change of monitor while the game runs;
  window → full screen → window three times in every mode.
* Maximum FPS: `present-policy-test` (the cap on a simulated clock: 20…240 and custom, game rates 60/59.94/50/30, even pacing, stall, change of limit,
  fast forward, no wait inside `Admit`), and in `present-sync-test` the real game with every limit × every mode × every monitor: VI/s, audio
  bytes per frame (core counter `AudioBytesProduced`) and the speed of the limiter are the same with and without a limit; presented/s follows
  min(limit, what the game makes); fast forward holds nothing back.
* `settings-test`: the combo, its three values, Auto by default, the "Current display" line.

Under Xvfb the context accepts no swap interval, so the tests also exercise the fallback (the driver refuses → 0). What only a real monitor
can show — tearing, smoothness, a second monitor, VRR — is the manual test below.

## Manual test

```
VORTEX_PRESENT_TRACE=1 build-final/bin/vortex64 2> present.log
```
1. Settings → Graphics: *Current display* shows your monitor and rate (e.g. `1920×1080 @ 100 Hz`).
2. Run a game; in `present.log` Auto must say `wanted 1` and, on a real driver, `applied 1`. No tearing; the game keeps its speed (the
   "emulator ... VI/s" line stays near 60 / 50, never near the Hz of the monitor).
3. Drag the window to another monitor with a different rate: a `display:` line and a new `hint sent` line appear; the game does not speed up.
4. Full screen and back several times: `applied` is the same afterwards.
5. Off must tear on a monitor of any rate; On equals Auto when the monitor is as fast as the game.
