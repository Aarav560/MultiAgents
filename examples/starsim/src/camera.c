#include "camera.h"
#include <math.h>

vec2 cam_to_screen(const camera *c, vec2 world_pt) {
    double screen_x = c->w / 2.0 + (world_pt.x - c->center.x) * c->scale;
    double screen_y = c->h / 2.0 - (world_pt.y - c->center.y) * c->scale;
    return v2(screen_x, screen_y);
}

vec2 cam_to_world(const camera *c, vec2 screen_pt) {
    double world_x = c->center.x + (screen_pt.x - c->w / 2.0) / c->scale;
    double world_y = c->center.y - (screen_pt.y - c->h / 2.0) / c->scale;
    return v2(world_x, world_y);
}

void cam_zoom_at(camera *c, vec2 screen_pt, double factor) {
    vec2 world_pt = cam_to_world(c, screen_pt);
    c->scale *= factor;
    if (c->scale < 1e-12) c->scale = 1e-12;
    if (c->scale > 1e12) c->scale = 1e12;
    c->center = v2_madd(world_pt, v2(screen_pt.x - c->w / 2.0, -(screen_pt.y - c->h / 2.0)), -1.0 / c->scale);
}

void cam_pan_pixels(camera *c, double dx, double dy) {
    c->center.x -= dx / c->scale;
    c->center.y += dy / c->scale;
}

void cam_fit(camera *c, vec2 center, double radius) {
    c->center = center;
    int smaller = c->w < c->h ? c->w : c->h;
    c->scale = smaller / 2.0 / radius;
}

void cam_follow(camera *c, vec2 target, double t) {
    c->center = v2_madd(c->center, v2_sub(target, c->center), t);
}
