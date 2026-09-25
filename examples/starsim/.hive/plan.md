goal: starsim - interactive windowed space simulator in pure C (Win32 / X11 / Cocoa, no libraries)
budget: balanced
max_parallel: 16
accept: make -s clean all test
accept: make -s headless && ./build/starsim-headless --shot build/accept.ppm --frames 60

## PHYS [builder deep] Physics: gravity, integrator, energy, collisions, path prediction
writes: src/physics.c, tests/test_physics.c
reads: include/sim.h, include/vec2.h, src/world.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_physics.c src/physics.c src/world.c -lm -o build/test_physics && ./build/test_physics
Implement the physics.c part of sim.h exactly. gravity_compute with GRAV_BH must call gravity_bh (declared in sim.h, implemented in quadtree.c by another worker).
For link independence, the test file defines a trivial `void gravity_bh(world *w, double theta) { (void)theta; gravity_direct(w); }`.
Softened accel: a_i = sum G m_j d / (|d|^2 + soft^2)^1.5. Leapfrog KDK: kick half, drift, recompute, kick half.
collide_merge: a uniform grid (cell = 2 * max radius) when n > 64, brute force otherwise, repeating passes until nothing merges. Keep it fast for 3,000 bodies (grid built once per pass).
predict_path: a test particle under frozen bodies (reuse the softened formula); stop when inside any body's radius.
Tests: a circular orbit returns to its start after one period (G = 1, M = 1, r = 1, dt = T/2000, error < 1e-3); energy is bounded over 50 orbits (< 1e-4);
momentum is conserved for 5 random bodies over 1,000 steps (1e-12 relative); a merge conserves mass and momentum exactly, gives r = sqrt(r1^2 + r2^2), and the heavier body keeps its id;
grid and brute force give the same merges for 1,000 random bodies (compare by building the brute-force count in the test); a chain merge; predict_path of a circular orbit stays at r = 1 within 1e-3;
world_energy of two bodies matches -G m1 m2 / r + KE.

## QUAD [builder deep] Barnes-Hut quadtree
writes: src/quadtree.c, tests/test_quadtree.c
reads: include/sim.h, include/vec2.h, src/world.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_quadtree.c src/quadtree.c src/world.c -lm -o build/test_quadtree && ./build/test_quadtree
Implement gravity_bh. It must be FAST: pooled node array reused across calls (a static buffer grown by doubling is fine: the app is single-threaded),
leaf buckets of up to 8 bodies, iterative traversal with an explicit stack, opening test s^2 < theta^2 d^2 (no sqrt until interaction), softening as in the header.
Depth cap 48 for coincident bodies. On allocation failure, compute the direct sum itself (don't call physics.c).
Tests: 2,000 random bodies (seeded LCG in the test): theta = 0 matches a static direct-sum helper to 1e-9 relative; theta = 0.5 gives median relative error < 1%;
dead bodies are ignored and get acc 0; a single body gets 0; coincident bodies don't crash; timing: 10,000 bodies at theta 0.7 in < 60 ms per call (print the time; fail only above 250 ms to avoid CI flakiness).

## SCEN [builder deep] Scenarios
writes: src/scenario.c, tests/test_scenario.c
reads: include/scenario.h, include/sim.h, src/world.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_scenario.c src/scenario.c src/world.c -lm -o build/test_scenario && ./build/test_scenario
Implement all 7 scenarios exactly as specified in the project context, with circular-velocity math inside scenario.c and your own seeded PRNG (xorshift or splitmix).
Use real planetary data for the Solar System (semi-major axis in AU / mass in solar masses): Mercury 0.387/1.66e-7, Venus 0.723/2.45e-6, Earth 1.0/3.0e-6, Mars 1.524/3.23e-7,
Jupiter 5.203/9.55e-4, Saturn 9.537/2.86e-4, Uranus 19.19/4.37e-5, Neptune 30.07/5.15e-5, Moon 0.00257 from Earth / 3.69e-8.
Pick good-looking colors (Earth blue, Mars red, Jupiter tan, Saturn pale gold, Uranus cyan, Neptune deep blue).
Galaxy stars should look like a real spiral: a two-armed log-spiral density enhancement, a yellowish core and bluish outskirts.
Tests: every scenario loads, has the expected counts (solar 10, binary 5, figure8 3, galaxy 3001, collision 3002, planet formation 701, earth-moon 9), is in the COM frame,
and has sane view values; the same seed gives identical bodies and a different seed a different galaxy; scenario_find handles names, "3" and bad input;
Earth's speed is 2*pi AU/yr within 2%; figure8 energy is about -1.287 (0.01).

## REND [builder] Software rasterizer
writes: src/render.c, tests/test_render.c
reads: include/render.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_render.c src/render.c src/world.c -lm -o build/test_render && ./build/test_render
Implement render.h exactly. Performance: canvas_line and canvas_disc are called thousands of times per frame. Clip first, and use integer inner loops where possible.
canvas_starfield: about 1 star per 900 px^2, with brightness and slight color variation from a hash of the cell coordinates (no storage), wrapping with the offset. Star sizes are 1 px, with occasional 2 px bright ones.
Tests: clipping never writes out of bounds (draw far off-canvas shapes, then check a sentinel canary buffer around px); a disc's pixel coverage is about pi r^2 (+-5% for r = 20);
line endpoints are lit; blend math (alpha 128 of white over black is about 128); add saturates at 255; resize; the PPM header and size (write into build/); the starfield is deterministic per seed and shifts with the offset.

## FONT [builder deep] Bitmap font
writes: src/font.c, tests/test_font.c
reads: include/font.h, include/render.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_font.c src/font.c src/world.c -lm -o build/test_font && ./build/test_font
Implement font.h with a complete, correct, legible 8x8 glyph table for ASCII 32..126 (the public-domain font8x8_basic by Daniel Hepper, bit 0 = leftmost pixel).
Accuracy matters: the HUD text must read correctly. After writing it, render every glyph as ASCII art to stdout in the test (`#` / `.`) and look at it yourself; fix any wrong glyph.
font.c must NOT call render.c (link independence): write pixels directly into c->px with your own clipped alpha blend.
Tests: glyph 'A' matches the known font8x8 rows; space is empty; unknown chars map to '?'; font_width handles newlines and scale; drawing clips at the canvas edges without crashing;
a scale 2 glyph covers exactly 4x the lit pixels of scale 1.

## CAM [builder fast] Camera
writes: src/camera.c, tests/test_camera.c
reads: include/camera.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_camera.c src/camera.c src/world.c -lm -o build/test_camera && ./build/test_camera
Implement camera.h exactly. Tests: to_screen/to_world round-trip; the world origin maps to the screen centre; y is flipped; zoom_at keeps the point under the cursor fixed (1e-9);
pan moves the content with the mouse; fit uses the smaller side; the clamps; follow with t = 1 reaches the target and t = 0.5 goes halfway.

## TRAIL [builder fast] Trails
writes: src/trails.c, tests/test_trails.c
reads: include/trails.h, include/sim.h, src/world.c
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/test_trails.c src/trails.c src/world.c -lm -o build/test_trails && ./build/test_trails
Implement trails.h exactly, using one flat preallocated array (max_bodies * max_points) plus a small id->slot map (a linear-probe hash) so trails_record is O(n) per frame.
Tests: the ring buffer wraps and returns oldest-first; dust is skipped; dead bodies release their slots and the slots get reused; more bodies than max_bodies are ignored gracefully;
trails_get on an unknown id returns 0; clear.

## NULLP [builder fast] Headless platform
writes: src/platform_null.c
reads: include/platform.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude -DSTARSIM_HEADLESS -c src/platform_null.c -o build/platform_null.o && x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude -DSTARSIM_HEADLESS -c src/platform_null.c -o build/platform_null_win.o
The whole file is inside `#if defined(STARSIM_HEADLESS)`. pf_open returns -1 (no display); pf_poll returns 0 with quit set; present, set_title and close are no-ops;
pf_time and pf_sleep are real (clock_gettime/nanosleep on POSIX with `_POSIX_C_SOURCE 200809L`, QueryPerformanceCounter/Sleep on _WIN32).

## X11 [builder] Linux X11 platform
writes: src/platform_x11.c, tests/demo_window.c
reads: include/platform.h
accept: mkdir -p build && cc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude tests/demo_window.c src/platform_x11.c -lX11 -lm -o build/demo_window && xvfb-run -a ./build/demo_window --frames 120
The whole file is inside `#if !defined(STARSIM_HEADLESS) && !defined(_WIN32) && !defined(__APPLE__)`. Use `#define _POSIX_C_SOURCE 200809L` before includes.
Xlib only (no XShm, no extensions): XCreateSimpleWindow and a WM_DELETE_WINDOW protocol for close; XImage over a malloc'd buffer (recreated on resize), so present uses XPutImage.
If the incoming framebuffer size differs from the window, do a nearest-neighbour stretch into the XImage buffer. Handle the depth-24/32 TrueColor visual (the common case) and return -1 from pf_open otherwise.
Map keys: XLookupKeysym to the platform.h codes (letters lowercase; XK_plus/XK_equal/XK_minus; bracketleft/right as '[' ']'; arrows, Escape, Return, Tab, BackSpace, F1, Shift, Control).
Mouse buttons 1/3/2 map to left/right/middle, and buttons 4/5 to the wheel +1/-1. ConfigureNotify sets w/h/resized. Don't block in pf_poll (use XPending).
tests/demo_window.c: opens 800x600 and animates a moving gradient plus a white square that follows the mouse. It runs `--frames N` frames (default: until closed) and exits 0,
printing "demo_window ok: N frames, WxH". It must work under xvfb-run.

## WIN32 [builder deep] Windows platform
writes: src/platform_win32.c
reads: include/platform.h, tests/demo_window.c
accept: mkdir -p build && x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude -c src/platform_win32.c -o build/platform_win32.o && x86_64-w64-mingw32-gcc -std=c11 -O2 -Iinclude -c src/world.c -o build/world_w.o
The whole file is inside `#if defined(_WIN32) && !defined(STARSIM_HEADLESS)`. Plain Win32 + GDI (link user32, gdi32): RegisterClassA/CreateWindowExA with AdjustWindowRect so the client area is w x h;
a WndProc that records input into a static pf_input; present via StretchDIBits with a top-down 32-bit BITMAPINFO (negative biHeight). SetProcessDPIAware (loaded dynamically from user32,
so older SDKs still work). Wheel: GET_WHEEL_DELTA_WPARAM / WHEEL_DELTA. Map VK codes to platform.h codes ('A'..'Z' become lowercase, VK_OEM_PLUS/VK_OEM_MINUS/VK_ADD/VK_SUBTRACT, VK_OEM_4/6 as '[' ']').
Capture the mouse while buttons are down (SetCapture/ReleaseCapture). WM_SIZE updates w/h/resized. WM_CLOSE sets quit. PeekMessage loop in pf_poll. pf_time via QueryPerformanceCounter;
pf_sleep via Sleep with timeBeginPeriod(1) (winmm, loaded dynamically or skipped). It must also compile with MSVC (no GCC-only extensions).

## COCOA [builder deep] macOS platform (C only, Objective-C runtime)
writes: src/platform_cocoa.c
reads: include/platform.h, tests/demo_window.c
accept: grep -q "objc_msgSend" src/platform_cocoa.c && cc -std=c11 -Wall -Wextra -Iinclude -fsyntax-only -DSTARSIM_COCOA_SYNTAX_ONLY src/platform_cocoa.c
The whole file is inside `#if defined(__APPLE__) && !defined(STARSIM_HEADLESS)`, so on Linux it compiles to nothing (that is all the accept can check here; macOS CI builds it for real).
Pure C11 using <objc/runtime.h>, <objc/message.h>, <CoreGraphics/CoreGraphics.h>, and casting objc_msgSend to the right function-pointer type for every call (required on arm64).
Create NSApplication (setActivationPolicy Regular, finishLaunching), an NSWindow (titled|closable|miniaturizable|resizable) and a custom NSView subclass registered at runtime with class_addMethod for:
drawRect: (build a CGImage from the framebuffer via CGDataProvider/CGImageCreate, with kCGImageAlphaNoneSkipFirst|kCGBitmapByteOrder32Little, and draw it into the current CGContext), acceptsFirstResponder,
isFlipped, keyDown:/keyUp: (map characters and keyCodes for arrows, esc, return, tab, delete, F1), mouseDown/Up/Dragged, rightMouseDown/Up/Dragged, scrollWheel:, and windowShouldClose: via a delegate class.
pf_poll pumps events with nextEventMatchingMask:untilDate:distantPast inMode:NSDefaultRunLoopMode dequeue:YES plus sendEvent:. present copies the buffer and calls setNeedsDisplay: YES, then displayIfNeeded.
Handle backing-scale (Retina) by drawing into the view bounds. pf_time via mach_absolute_time; pf_sleep via nanosleep. Keep it careful and well-commented, because it can't be run here.

## APP [integrator deep] Main loop, controls, drawing, build files
writes: src/app.c, src/main.c, tests/test_app.c, Makefile, build.bat
reads: include/app.h, include/platform.h, include/render.h, include/font.h, include/camera.h, include/trails.h, include/scenario.h, include/sim.h
deps: PHYS, QUAD, SCEN, REND, FONT, CAM, TRAIL, NULLP, X11, WIN32, COCOA
accept: make -s clean all test && make -s headless && ./build/starsim-headless --shot build/app.ppm --frames 60
Implement the app exactly as the project context describes (drawing order, HUD, help overlay, controls, time warp budget, launch tool, CLI). main.c only calls app_main.
Keep app.c organized: an `app` state struct and functions like handle_input, step_physics, draw_world, draw_hud, draw_help, draw_launch_preview.
app_render_frames drives the same update/draw code with synthetic no-input frames (no window) so tests and screenshots exercise the real renderer.
### Makefile
CC ?= cc, CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude, and objects in build/. OS detection: Darwin gives platform_cocoa.c plus `-framework Cocoa -framework CoreGraphics`,
anything else gives platform_x11.c plus -lX11 (build/starsim). The `headless` target builds build/starsim-headless with -DSTARSIM_HEADLESS and platform_null.c (no X11 needed).
`test` builds and runs every tests/test_*.c linked against all library objects (no platform, no app.c except test_app, which links app.c plus platform_null built with -DSTARSIM_HEADLESS).
Other targets: `run`, `clean`.
### build.bat
Like examples: find gcc, clang or cl; build build\starsim.exe from all src\*.c (the platform files self-select by #if) linking user32 gdi32 (and winmm if timeBeginPeriod is used);
`build.bat test` also builds and runs the tests (tests link with STARSIM_HEADLESS + platform_null for test_app). Print install hints if no compiler is found.
Note: `set OUT=-o ` has a trailing-space pitfall; use `-o` with the path in a separate token.
### tests/test_app.c
app_render_frames for every scenario at 640x400 for 30 frames into build/; each file exists with the right size, and the image is not blank (count lit pixels > 500).

## DOCS [scribe standard] README
writes: README.md
Write README.md: what starsim is (with a short feature list); build and run on Windows (build.bat, needs gcc/clang/MSVC, with `winget install BrechtSanders.WinLibs.POSIX.UCRT` as an example), Linux
(`sudo apt install libx11-dev` or the dnf/pacman equivalent, then make), macOS (Xcode command-line tools: `xcode-select --install`, then make); the controls table; the 7 scenarios with one line each;
the command-line options; headless screenshots (`make headless`, `--shot`); how it's built (module list) and that it was built by hive agents in parallel from one plan (.hive/plan.md). Plain and concise.

## VERIFY [verifier] End-to-end verification
writes: tests/run_checks.sh
deps: APP, DOCS
accept: sh tests/run_checks.sh
Write tests/run_checks.sh (POSIX sh, set -eu), which:
1. make clean all test;
2. make headless, then renders every scenario with --shot into build/shots/<n>.ppm (60 frames) and checks that the files exist;
3. runs the windowed build/starsim under `xvfb-run -a` with `--frames 90`, or if the app has no such flag, with a `timeout 5` and accepts exit 124 as success;
4. cross-compiles everything for Windows with x86_64-w64-mingw32-gcc (`-lgdi32 -luser32 -lwinmm`) if that compiler is installed, else skips with a note.
Run it. For every failure in another worker's file, `ask` with the file, line and fix. Report the counts in your done note.
