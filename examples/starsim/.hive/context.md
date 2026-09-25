# Project context: `starsim`, an interactive space simulator in pure C

## Goal
A real-time, windowed, interactive gravity simulator. When the user runs it, a window opens showing
glowing stars and planets on orbit trails over a star field. They can pan, zoom, follow bodies,
launch new bodies with a predicted-path preview, warp time and switch between 7 scenarios. It
must build and run on **Windows, Linux and macOS with no third-party libraries**: Win32/GDI,
X11 or Cocoa through the Objective-C runtime, plus libc and libm.

## The contract is in the headers (read the ones your task names; never change them)
`include/vec2.h sim.h scenario.h render.h font.h camera.h trails.h platform.h app.h`.
`src/world.c` (world bookkeeping) and `tests/test.h` (CHECK, CHECK_NEAR, CHECK_REL, RUN,
TEST_SUMMARY) are already written. If a header seems wrong, `ask`, don't edit.

## Conventions
- C11. Every file must compile clean with `-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude`
  on gcc and clang, and with MinGW. `static` for everything not in a header. No globals except in the
  platform backends (one window) and app.c (app state struct).
- Library code (everything but app.c, main.c and the platforms) never prints and never exits.
- Colors are `uint32_t` 0x00RRGGBB. Canvas pixels are row-major with the top row first.
- Performance matters: it runs at 60 fps. No per-frame malloc in hot paths (reuse buffers), and no O(n^2)
  work per frame on scenes over 2,000 bodies except gravity_direct when chosen.
- Tests use tests/test.h, are deterministic, run in under 2 s and check real behaviour (conservation laws,
  exact pixels, round trips). A module's test compiles with **only its own .c plus src/world.c**
  (modules are written in parallel):
  `mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_X.c src/X.c src/world.c -lm -o build/test_X && ./build/test_X`
  If a test needs gravity, it writes a tiny static direct-sum helper.
- 4-space indent, snake_case, short functions, comments only where the math or OS API is non-obvious.

## How the app behaves (APP implements it; DOCS documents it)
Window: 1280x800 by default, resizable, title "starsim - <scenario name>".
Each frame: poll input, run physics (see time warp), record trails every frame, then render:
1. Background `rgb(4,6,12)`, then `canvas_starfield(seed, -cam.center * scale * 0.05)` for subtle parallax.
2. Trails: polylines from trails_get, fading from transparent (oldest) to alpha 150 (newest) in the
   body's color. Toggle with T.
3. Bodies, by kind:
   - Stars: glow radius max(12, 6*r_px) at intensity 0.9, then a disc.
   - Black holes: a black disc with an orange ring.
   - Planets and moons: a disc with a thin darker ring.
   - Dust and asteroids: a single blended pixel, or a tiny disc when r_px > 1.
   Drawn radius r_px = max(radius*scale, min_px) with min_px star 3, planet 2, moon 1.5, others 1.
4. The selected body gets a ring plus its name label.
5. Spawn preview while right-dragging: a line from the press point to the mouse, and the predicted path (predict_path, 600 points) as a dotted line.
6. HUD panel top-left (semi-transparent `rgb(10,14,28)` alpha 170, 8 px padding, font scale 2):
   scenario name, `t = <time> <unit>`, `bodies N`, `warp Nx` (or `PAUSED`), `fps N`,
   `gravity: direct|Barnes-Hut`, `energy drift 1.2e-5` (every 30 frames, only if n <= 3000).
   Bottom-left, font scale 1: `H: help`. Help overlay (H or F1) centred: the controls table below.
Controls:
| input | action |
|---|---|
| left-drag on empty space | pan |
| mouse wheel | zoom around the cursor (factor 1.15 per notch) |
| left-click on a body (within 10 px) | select and follow it; click empty space to stop following |
| right-drag | launch a body: press = position, drag vector = velocity (velocity = drag_px / scale * launch_speed), release = add; the preview shows its predicted path |
| 1..7 | load scenario N |
| space | pause / resume |
| + / = and - | time warp x2 / /2 (1/8x .. 512x) |
| [ and ] | cycle the kind to launch: asteroid, planet, star, black hole (shown in HUD) |
| t | toggle trails |
| g | toggle gravity solver (direct / Barnes-Hut) |
| c | toggle collisions |
| f | fit the whole system in view |
| r | reload the current scenario |
| h or F1 | help overlay |
| Esc | quit |
Physics per frame: `steps = steps_per_frame * warp` (warp < 1 scales dt down instead), stopping early if
the frame's physics exceeds 12 ms (the HUD then shows `(slowed)`). After the steps, collide_merge (if
collisions are on) and world_compact when anything merged. Launched body masses: asteroid 1e-9*M,
planet 1e-6*M, star 0.3*M, black hole 2*M, where M is the heaviest body's mass. Radius from density,
relative to the heaviest body.
Command line: `starsim [--scenario N|name] [--width W] [--height H] [--seed S] [--shot FILE.ppm --frames N]`.
`--shot` renders headless via app_render_frames and exits (no window). `--help` prints usage.
If pf_open fails, print "no display available" plus a hint and return 1.

## Scenarios (SCEN implements them; all end with world_recenter)
1. **Solar System**: G = 4*pi^2 (AU, years, solar masses). Sun (1.0, yellow-white `rgb(255,240,200)`), all 8 planets with real
   mean distances (AU), real masses in solar masses and distinct colors, on circular orbits with phases spread out, plus the Moon
   around Earth. Radii are exaggerated for visibility (Sun 0.02, planets 0.004..0.01). dt 0.0005 yr, 4 steps per frame,
   view_radius 1.8 (inner system; outer planets are off screen until zoomed out). Units "years".
2. **Binary Star**: two stars 1.0 and 0.6, a = 1 AU, e = 0.3, plus 3 circumbinary planets at 3, 4.2 and 5.5 AU. G = 4*pi^2, dt 0.0005,
   4 steps per frame, view 7.
3. **Figure Eight**: G = 1, three unit masses, Chenciner-Montgomery initial conditions, distinct colors. dt 0.001, 8 steps per frame, view 1.4.
4. **Galaxy**: G = 1, central black hole mass 1 (radius 0.02), plus 3000 stars (mass 2e-5, dust kind, blue-white-yellow tint) on a
   two-armed exponential disk (scale radius 1, r in [0.1, 4]), with circular velocities for the enclosed mass. Softening 0.02, BH gravity,
   dt 0.002, 2 steps per frame, view 4.5.
5. **Galaxy Collision**: two disks of 1500 stars each (as in 4, each with its own black hole), on an inclined parabolic encounter:
   start 8 apart, closing. The disks have different tints. BH, dt 0.004, 2 steps per frame, view 9.
6. **Planet Formation**: G = 4*pi^2, a star of mass 1 plus 700 dust/planetesimals (mass 3e-8..3e-7, radius from mass) on nearly
   circular orbits from 0.4 to 3 AU with small random eccentricity. Collisions on. dt 0.001, 4 steps per frame, view 3.5.
7. **Earth and Moon**: G = 1 units scaled so Earth = 1 and the Moon = 0.0123 at distance 1, plus 6 satellites (kind ship) in low
   and high orbits and one on an escape trajectory. dt 0.0005, 8 steps per frame, view 1.4.
seed makes every random scenario reproducible.

## Layout
```
include/   the headers above
src/       world.c physics.c quadtree.c scenario.c render.c font.c camera.c trails.c
           platform_win32.c platform_x11.c platform_cocoa.c platform_null.c app.c main.c
tests/     test.h test_<module>.c demo_window.c
Makefile build.bat README.md
```
