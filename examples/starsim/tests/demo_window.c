/* demo_window.c - manual/CI smoke test for a platform backend: opens an 800x600 window and
 * animates a moving gradient plus a white square that follows the mouse. Runs --frames N frames
 * (default: until the window is closed) and exits 0, printing "demo_window ok: N frames, WxH".
 * Works under xvfb-run. */
#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    int frame_limit = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frame_limit = atoi(argv[i + 1]);
            i++;
        }
    }

    int w = 800, h = 600;
    if (pf_open(w, h, "starsim demo_window") != 0) {
        fprintf(stderr, "no display available\n");
        return 1;
    }

    uint32_t *fb = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!fb) {
        pf_close();
        return 1;
    }

    pf_input in;
    memset(&in, 0, sizeof(in));
    in.w = w;
    in.h = h;

    long frame = 0;
    double t0 = pf_time();

    while (1) {
        if (!pf_poll(&in)) break;
        if (in.quit) break;

        if (in.resized && in.w > 0 && in.h > 0) {
            w = in.w;
            h = in.h;
            uint32_t *nfb = (uint32_t *)realloc(fb, (size_t)w * (size_t)h * sizeof(uint32_t));
            if (nfb) fb = nfb;
        }

        double t = pf_time() - t0;

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                unsigned r = (unsigned)((x * 255) / (w > 1 ? w - 1 : 1));
                unsigned g = (unsigned)((y * 255) / (h > 1 ? h - 1 : 1));
                unsigned b = (unsigned)(128 + 127.0 * (t - (double)(long)t));
                (void)t;
                b = (unsigned)((int)(t * 60.0) % 256);
                fb[(size_t)y * (size_t)w + (size_t)x] =
                    ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
            }
        }

        int sq = 24;
        int mx = in.mouse_x, my = in.mouse_y;
        int x0 = mx - sq / 2, y0 = my - sq / 2;
        for (int dy = 0; dy < sq; dy++) {
            int y = y0 + dy;
            if (y < 0 || y >= h) continue;
            for (int dx = 0; dx < sq; dx++) {
                int x = x0 + dx;
                if (x < 0 || x >= w) continue;
                fb[(size_t)y * (size_t)w + (size_t)x] = 0x00FFFFFFu;
            }
        }

        pf_present(fb, w, h);

        frame++;
        if (frame_limit >= 0 && frame >= frame_limit) break;
    }

    printf("demo_window ok: %ld frames, %dx%d\n", frame, w, h);

    free(fb);
    pf_close();
    return 0;
}
