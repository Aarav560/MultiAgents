/* orbit.c - Keplerian two-body mechanics. */
#include "orbit.h"

#include <math.h>

#define ORBIT_TWO_PI 6.283185307179586476925286766559
#define ORBIT_PI 3.1415926535897932384626433832795
#define ORBIT_EPS 1e-10

static double wrap_2pi(double x) {
    double y = fmod(x, ORBIT_TWO_PI);
    if (y < 0.0) y += ORBIT_TWO_PI;
    if (y >= ORBIT_TWO_PI) y = 0.0;
    return y;
}

/* Signed angle from unit vector u to vector w, measured about the unit normal n. */
static double angle_about(vec3 u, vec3 w, vec3 n) {
    return atan2(vec3_dot(vec3_cross(u, w), n), vec3_dot(u, w));
}

int orbit_from_state(vec3 r, vec3 v, double mu, orbit_elements *out) {
    if (!out || !(mu > 0.0)) return -1;
    double rl = vec3_len(r), v2 = vec3_len2(v);
    if (!(rl > 0.0) || !isfinite(rl) || !isfinite(v2)) return -1;
    vec3 h = vec3_cross(r, v);
    double hl = vec3_len(h);
    if (!(hl > 1e-14 * rl * sqrt(v2 + mu / rl))) return -1;
    vec3 hhat = vec3_scale(h, 1.0 / hl);

    /* Eccentricity vector: ((v^2 - mu/r) r - (r.v) v) / mu */
    vec3 ev = vec3_scale(vec3_sub(vec3_scale(r, v2 - mu / rl), vec3_scale(v, vec3_dot(r, v))), 1.0 / mu);
    double e = vec3_len(ev);
    double energy = 0.5 * v2 - mu / rl;
    if (fabs(1.0 - e) < 1e-12 || energy == 0.0) return -1;
    double a = -mu / (2.0 * energy);

    double ci = hhat.z;
    if (ci > 1.0) ci = 1.0;
    if (ci < -1.0) ci = -1.0;
    double inc = acos(ci);
    double sin_i = sqrt(hhat.x * hhat.x + hhat.y * hhat.y);

    vec3 nhat;
    double raan;
    if (inc < ORBIT_EPS || sin_i < ORBIT_EPS) {
        nhat = vec3_make(1.0, 0.0, 0.0);
        raan = 0.0;
    } else {
        nhat = vec3_norm(vec3_make(-h.y, h.x, 0.0));   /* z x h */
        raan = wrap_2pi(atan2(nhat.y, nhat.x));
    }

    double argp, nu;
    if (e < ORBIT_EPS) {
        argp = 0.0;
        nu = angle_about(nhat, r, hhat);
    } else {
        vec3 ehat = vec3_scale(ev, 1.0 / e);
        argp = angle_about(nhat, ehat, hhat);
        nu = angle_about(ehat, r, hhat);
    }

    out->a = a;
    out->e = e;
    out->i = inc;
    out->raan = raan;
    out->argp = wrap_2pi(argp);
    out->nu = wrap_2pi(nu);
    return 0;
}

int orbit_to_state(const orbit_elements *el, double mu, vec3 *r, vec3 *v) {
    if (!el || !r || !v || !(mu > 0.0)) return -1;
    double e = el->e;
    double p = el->a * (1.0 - e * e);   /* semi-latus rectum; > 0 for ellipses and hyperbolas */
    if (!(p > 0.0) || e < 0.0) return -1;
    double cn = cos(el->nu), sn = sin(el->nu);
    double denom = 1.0 + e * cn;
    if (!(denom > 0.0)) return -1;
    double rad = p / denom;
    double vs = sqrt(mu / p);

    /* Perifocal frame, then rotate by R3(-raan) R1(-i) R3(-argp). */
    double xp = rad * cn, yp = rad * sn;
    double vxp = -vs * sn, vyp = vs * (e + cn);
    double cO = cos(el->raan), sO = sin(el->raan);
    double cw = cos(el->argp), sw = sin(el->argp);
    double ci = cos(el->i), si = sin(el->i);
    double r11 = cO * cw - sO * sw * ci, r12 = -cO * sw - sO * cw * ci;
    double r21 = sO * cw + cO * sw * ci, r22 = -sO * sw + cO * cw * ci;
    double r31 = sw * si, r32 = cw * si;

    *r = vec3_make(r11 * xp + r12 * yp, r21 * xp + r22 * yp, r31 * xp + r32 * yp);
    *v = vec3_make(r11 * vxp + r12 * vyp, r21 * vxp + r22 * vyp, r31 * vxp + r32 * vyp);
    return 0;
}

double orbit_period(double a, double mu) {
    if (!(a > 0.0) || !(mu > 0.0)) return 0.0;
    return ORBIT_TWO_PI * sqrt(a * a * a / mu);
}

/* Kepler's equation M = E - e sin E, Newton iteration on M reduced to [-pi, pi]. */
double orbit_kepler_E(double M, double e) {
    if (!(e >= 0.0 && e < 1.0) || !isfinite(M)) return NAN;
    double k = floor((M + ORBIT_PI) / ORBIT_TWO_PI);
    double m = M - k * ORBIT_TWO_PI;
    double E = e < 0.8 ? m : (m >= 0.0 ? ORBIT_PI : -ORBIT_PI);
    for (int it = 0; it < 100; it++) {
        double f = E - e * sin(E) - m;
        double d = f / (1.0 - e * cos(E));
        E -= d;
        if (fabs(d) < 1e-14) break;
    }
    return E + k * ORBIT_TWO_PI;
}

double orbit_true_from_mean(double M, double e) {
    double E = orbit_kepler_E(M, e);
    if (isnan(E)) return NAN;
    /* tan(nu/2) = sqrt((1+e)/(1-e)) tan(E/2) */
    double nu = 2.0 * atan2(sqrt(1.0 + e) * sin(0.5 * E), sqrt(1.0 - e) * cos(0.5 * E));
    return nu;
}

double orbit_vis_viva(double r, double a, double mu) {
    if (!(r > 0.0) || a == 0.0) return 0.0;
    double s = mu * (2.0 / r - 1.0 / a);
    return s > 0.0 ? sqrt(s) : 0.0;
}
