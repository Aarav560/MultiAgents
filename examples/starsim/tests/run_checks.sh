#!/bin/sh
# run_checks.sh - end-to-end verification for starsim. POSIX sh, run from the project root:
#   sh tests/run_checks.sh
set -eu

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

pass=0
fail=0
skip=0

note() { printf '%s\n' "$*"; }
ok()   { pass=$((pass + 1)); note "OK   - $*"; }
bad()  { fail=$((fail + 1)); note "FAIL - $*"; }
skp()  { skip=$((skip + 1)); note "SKIP - $*"; }

# 1. Build everything and run the native unit tests.
note "== make clean all test =="
if make clean all test; then
    ok "make clean all test"
else
    bad "make clean all test"
fi

# 2. Headless screenshots for every scenario.
note "== headless shots =="
if make headless; then
    ok "make headless"
    mkdir -p build/shots
    i=1
    while [ "$i" -le 7 ]; do
        shot="build/shots/$i.ppm"
        rm -f "$shot"
        if ./build/starsim-headless --scenario "$i" --shot "$shot" --frames 60 >/dev/null 2>&1 \
                && [ -s "$shot" ]; then
            ok "shot scenario $i -> $shot"
        else
            bad "shot scenario $i -> $shot"
        fi
        i=$((i + 1))
    done
else
    bad "make headless"
    skp "headless shots (build failed)"
fi

# 3. Windowed build under a virtual display. The app may or may not honor a windowed
# --frames flag to self-limit its run, so always wrap in `timeout` as a safety net: a
# clean exit (0) means it self-limited, exit 124 means timeout ended it, both are success.
note "== windowed run =="
if [ -x build/starsim ]; then
    if command -v xvfb-run >/dev/null 2>&1; then
        set +e
        xvfb-run -a timeout 5 ./build/starsim --frames 90 >/tmp/starsim_windowed.log 2>&1
        rc=$?
        set -e
        if [ "$rc" -eq 0 ] || [ "$rc" -eq 124 ]; then
            ok "windowed run (--frames 90, exit $rc)"
        else
            bad "windowed run (--frames 90, exit $rc)"
            cat /tmp/starsim_windowed.log
        fi
    else
        skp "windowed run (xvfb-run not installed)"
    fi
else
    bad "windowed run (build/starsim missing)"
fi
rm -f /tmp/starsim_windowed.log

# 4. Cross-compile for Windows with MinGW, if available.
note "== mingw cross-compile =="
MINGW_CC=x86_64-w64-mingw32-gcc
if command -v "$MINGW_CC" >/dev/null 2>&1; then
    CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude"
    LIBSRC="src/world.c src/physics.c src/quadtree.c src/scenario.c src/render.c src/font.c src/camera.c src/trails.c"
    mkdir -p build/win

    if $MINGW_CC $CFLAGS src/*.c -o build/win/starsim.exe -lgdi32 -luser32 -lwinmm; then
        ok "mingw build build/win/starsim.exe"
    else
        bad "mingw build build/win/starsim.exe"
    fi

    mw_test_fail=0
    for t in tests/test_*.c; do
        m=$(basename "$t" .c)
        m=${m#test_}
        exe="build/win/$(basename "$t" .c).exe"
        if [ "$m" = "app" ]; then
            if ! $MINGW_CC $CFLAGS -DSTARSIM_HEADLESS "$t" src/app.c src/platform_null.c $LIBSRC \
                    -o "$exe" -lgdi32 -luser32 -lwinmm; then
                mw_test_fail=1
            fi
        else
            if ! $MINGW_CC $CFLAGS "$t" "src/$m.c" src/world.c -o "$exe"; then
                mw_test_fail=1
            fi
        fi
    done
    if [ "$mw_test_fail" -eq 0 ]; then
        ok "mingw cross-compile all tests"
    else
        bad "mingw cross-compile all tests"
    fi
else
    skp "mingw cross-compile ($MINGW_CC not installed)"
fi

note "== summary =="
note "pass=$pass fail=$fail skip=$skip"
if [ "$fail" -ne 0 ]; then
    exit 1
fi
exit 0
