/* body.h - one simulated object. SI units unless a scenario sets G = 1 (N-body units). */
#ifndef ORBIT_BODY_H
#define ORBIT_BODY_H

#include "vec3.h"

#define BODY_NAME_MAX 32

typedef enum {
    BODY_STAR = 0,
    BODY_PLANET,
    BODY_MOON,
    BODY_ASTEROID,
    BODY_SPACECRAFT,
    BODY_PARTICLE
} body_kind;

typedef struct {
    unsigned char r, g, b;
} rgb;

typedef struct {
    char name[BODY_NAME_MAX]; /* NUL-terminated, may be empty */
    body_kind kind;
    double mass;              /* kg (or N-body mass units) */
    double radius;            /* m; used for collisions and rendering */
    vec3 pos;                 /* m */
    vec3 vel;                 /* m/s */
    vec3 acc;                 /* m/s^2, written by the gravity solvers */
    rgb color;
    int alive;                /* 1 = simulated; 0 = merged away, removed by world_compact */
    int id;                   /* unique, stable, assigned by world_add */
} body;

/* Returns a zeroed body with alive = 1, kind = BODY_PARTICLE, color white, id = -1,
   and the name copied (truncated) from name (NULL allowed). Defined in world.c. */
body body_make(const char *name, body_kind kind, double mass, double radius, vec3 pos, vec3 vel);

/* Short lowercase name ("star", "planet", ...). Defined in world.c. */
const char *body_kind_name(body_kind k);

#endif
