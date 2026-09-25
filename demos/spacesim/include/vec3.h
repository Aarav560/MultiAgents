/* vec3.h - 3D vector math (header-only). Units are whatever the caller uses. */
#ifndef ORBIT_VEC3_H
#define ORBIT_VEC3_H

#include <math.h>

typedef struct {
    double x, y, z;
} vec3;

static inline vec3 vec3_make(double x, double y, double z) { vec3 v = {x, y, z}; return v; }
static inline vec3 vec3_zero(void) { return vec3_make(0.0, 0.0, 0.0); }
static inline vec3 vec3_add(vec3 a, vec3 b) { return vec3_make(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline vec3 vec3_sub(vec3 a, vec3 b) { return vec3_make(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline vec3 vec3_scale(vec3 a, double s) { return vec3_make(a.x * s, a.y * s, a.z * s); }
/* a + b * s, the workhorse of every integrator */
static inline vec3 vec3_madd(vec3 a, vec3 b, double s) { return vec3_make(a.x + b.x * s, a.y + b.y * s, a.z + b.z * s); }
static inline double vec3_dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline vec3 vec3_cross(vec3 a, vec3 b) {
    return vec3_make(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline double vec3_len2(vec3 a) { return vec3_dot(a, a); }
static inline double vec3_len(vec3 a) { return sqrt(vec3_len2(a)); }
static inline double vec3_dist(vec3 a, vec3 b) { return vec3_len(vec3_sub(a, b)); }
/* Unit vector; returns the zero vector for a zero-length input. */
static inline vec3 vec3_norm(vec3 a) {
    double l = vec3_len(a);
    return l > 0.0 ? vec3_scale(a, 1.0 / l) : vec3_zero();
}
static inline vec3 vec3_lerp(vec3 a, vec3 b, double t) { return vec3_madd(a, vec3_sub(b, a), t); }

#endif
