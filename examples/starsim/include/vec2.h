/* vec2.h - 2D vector math (header-only). */
#ifndef STARSIM_VEC2_H
#define STARSIM_VEC2_H

#include <math.h>

typedef struct {
    double x, y;
} vec2;

static inline vec2 v2(double x, double y) { vec2 v = {x, y}; return v; }
static inline vec2 v2_add(vec2 a, vec2 b) { return v2(a.x + b.x, a.y + b.y); }
static inline vec2 v2_sub(vec2 a, vec2 b) { return v2(a.x - b.x, a.y - b.y); }
static inline vec2 v2_scale(vec2 a, double s) { return v2(a.x * s, a.y * s); }
/* a + b * s */
static inline vec2 v2_madd(vec2 a, vec2 b, double s) { return v2(a.x + b.x * s, a.y + b.y * s); }
static inline double v2_dot(vec2 a, vec2 b) { return a.x * b.x + a.y * b.y; }
/* z component of the 3D cross product */
static inline double v2_cross(vec2 a, vec2 b) { return a.x * b.y - a.y * b.x; }
static inline double v2_len2(vec2 a) { return a.x * a.x + a.y * a.y; }
static inline double v2_len(vec2 a) { return sqrt(v2_len2(a)); }
static inline double v2_dist(vec2 a, vec2 b) { return v2_len(v2_sub(a, b)); }
static inline vec2 v2_norm(vec2 a) {
    double l = v2_len(a);
    return l > 0.0 ? v2_scale(a, 1.0 / l) : v2(0.0, 0.0);
}
/* Rotated 90 degrees counter-clockwise. */
static inline vec2 v2_perp(vec2 a) { return v2(-a.y, a.x); }

#endif
