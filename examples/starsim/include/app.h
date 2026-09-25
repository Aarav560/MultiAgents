/* app.h - the interactive simulator: main loop, controls, drawing. */
#ifndef STARSIM_APP_H
#define STARSIM_APP_H

/* Parses argv and runs. Returns a process exit code. */
int app_main(int argc, char **argv);

/* Headless: loads scenario (0-based), simulates frames frames at 1x warp and 60 fps and writes the
   final w x h frame (HUD included) to ppm_path. Returns 0 on success. Used by tests and --shot. */
int app_render_frames(int scenario, int w, int h, int frames, const char *ppm_path);

#endif
