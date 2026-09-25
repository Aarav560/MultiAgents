/* camera.h - world <-> screen mapping. Screen y grows downward; world y grows upward. */
#ifndef STARSIM_CAMERA_H
#define STARSIM_CAMERA_H

#include "vec2.h"

typedef struct {
    vec2 center;            /* world point at the middle of the screen */
    double scale;           /* pixels per world unit (> 0) */
    int w, h;               /* screen size in pixels */
} camera;

vec2 cam_to_screen(const camera *c, vec2 world_pt);
vec2 cam_to_world(const camera *c, vec2 screen_pt);
/* Multiplies scale by factor while keeping the world point under screen_pt fixed. Clamps scale
   to [1e-12, 1e12]. */
void cam_zoom_at(camera *c, vec2 screen_pt, double factor);
/* Moves the view by a mouse drag of (dx, dy) pixels (content follows the mouse). */
void cam_pan_pixels(camera *c, double dx, double dy);
/* Centres on center so that radius fits in half the smaller screen side. */
void cam_fit(camera *c, vec2 center, double radius);
/* Moves center a fraction t (0..1) of the way toward target (smooth follow). */
void cam_follow(camera *c, vec2 target, double t);

#endif
