# Physics of `orbit`

This document explains the physical model and numerical methods behind `orbit`: Newtonian
gravity and softening, the two gravity solvers (direct summation and Barnes-Hut), the five
integrators, Keplerian orbital mechanics, the Hohmann transfer, collision handling, the
diagnostics used to check a run's correctness, and N-body units. All math here is plain
arithmetic; no LaTeX is used anywhere in the codebase or its docs.

## 1. Newtonian gravity and softening

Every body attracts every other body according to Newton's law of universal gravitation. The
force on body `i` from body `j` is

```
F_ij = G * m_i * m_j * (r_j - r_i) / |r_j - r_i|^3
```

where `G` is the gravitational constant, `m_i` and `m_j` are the masses, and `r_i`, `r_j` are
position vectors. Dividing by `m_i` gives the acceleration contribution from `j`:

```
a_i += G * m_j * (r_j - r_i) / |r_j - r_i|^3
```

Summed over every other alive body, this is the direct-summation force law used throughout
`orbit`.

### The softening problem

If two bodies pass very close to each other, `|r_j - r_i|` approaches zero and the `1/r^2`
force blows up, which both breaks energy conservation for any finite timestep and produces
physically meaningless slingshots. `orbit` avoids this with a softening length `eps`
(`world.softening`), replacing the true distance with a softened one:

```
a_i = sum_j  G * m_j * (r_j - r_i) / (r^2 + eps^2)^(3/2)
```

where `r = |r_j - r_i|`. As `r -> 0` the acceleration now saturates at a large but finite
value instead of diverging. Softening can be read as smearing each point mass into a small
Plummer-like sphere of radius on the order of `eps`, or equivalently as a numerical regularizer
that trades a small bias in close encounters for stability at the chosen timestep. Physically
faithful runs (real planets and moons, which never actually collide at simulation resolution)
use a very small or zero `eps`; loosely sampled systems such as the `cluster` and `disk`
scenarios need `eps` on the order of the mean inter-particle spacing, or close encounters
between point masses would dominate the dynamics with spurious high-velocity kicks.

The potential energy used by `diagnostics_compute` and `gravity_potential` uses the same
softened denominator so that energy conservation checks are consistent with the force actually
being integrated:

```
U = - sum_{i<j}  G * m_i * m_j / sqrt(r_ij^2 + eps^2)
```

## 2. Direct summation vs. Barnes-Hut

### Direct summation: O(n^2)

`gravity_direct` computes every pairwise interaction once (using Newton's third law: the force
on `j` from `i` is the exact negative of the force on `i` from `j`, scaled by mass), giving the
exact force under the softened law above. With `n` bodies this is `n * (n-1) / 2` pair
evaluations, i.e. O(n^2) work per step. It is exact (to floating-point precision) and simple,
and is the right choice whenever `n` is small to moderate (up to a few thousand bodies,
depending on how many steps are needed) or when exactness matters more than speed, such as
validating an integrator's energy conservation.

### Barnes-Hut: O(n log n)

For larger `n`, `orbit` implements the Barnes-Hut tree algorithm (`octree.h`). The idea: group
distant bodies together and treat a whole cluster as a single point mass at its center of mass,
rather than summing every individual pair.

The domain is recursively subdivided into an octree: each node is a cube; if a cube contains
more than one body it is split into 8 equal sub-cubes (octants), each becoming a child node,
until every leaf holds (in the common case) a single body. Every internal node caches the total
mass and center of mass of everything beneath it, computed bottom-up as the tree is built.

To compute the acceleration at a point, the tree is walked from the root. At each node the
**opening criterion** decides whether the node is treated as a single point mass or opened up
to examine its children individually:

```
s / d < theta
```

where `s` is the node's cube side length, `d` is the distance from the query point to the
node's center of mass, and `theta` is the opening angle (a tunable parameter, typically 0.3 to
0.8). If the criterion holds, the node's aggregate mass and center of mass are used directly and
the walk does not descend further; otherwise the walk recurses into the node's children. Leaves
are always evaluated exactly (excluding the body itself when `skip_index` matches).

Building the tree costs O(n log n) (each body is inserted by walking O(log n) levels on
average), and evaluating the acceleration on all `n` bodies costs O(n log n) as well, since each
query touches O(log n) nodes on average. This is the whole payoff over direct summation's
O(n^2): for `n` in the thousands or more, O(n log n) is dramatically cheaper.

### The theta trade-off

`theta` trades accuracy for speed:

- `theta = 0` disables the approximation entirely -- every node is opened all the way to
  leaves, so Barnes-Hut degenerates to direct summation. `orbit` guarantees this reproduces
  `gravity_direct` to within `1e-9` relative error, which is the correctness check used in
  `tests/test_octree.c`.
- Small `theta` (0.2-0.3) opens nodes aggressively, giving high accuracy close to direct
  summation but less speedup.
- Large `theta` (0.7-1.0) accepts more distant clusters as single point masses sooner, trading
  accuracy for a faster walk. Errors show up first as small torques from asymmetric mass
  distributions being treated as symmetric, and manifest as energy and angular momentum drift
  over long integrations.

A typical `theta` of 0.5 keeps relative force errors well under 1% for smoothly distributed
systems while cutting node visits sharply compared to `theta = 0`.

Because bodies can coincide or cluster arbitrarily tightly, `orbit` caps octree subdivision
depth at 64 and lets the deepest leaf hold multiple bodies rather than subdividing forever.

## 3. Integrators

`orbit` implements five integrators, selectable via `--integrator` or `integrator_kind`. All
of them advance `world.t` by `dt` and increment `world.step`, and all call the supplied
`accel_fn` themselves rather than trusting any acceleration left over from a previous step.

### Explicit (forward) Euler -- order 1, non-symplectic

The simplest possible scheme: evaluate acceleration once at the start of the step, then advance
position and velocity together using that acceleration.

```
a  = accel(x)
v_new = v + a * dt
x_new = x + v * dt
```

Euler is first-order accurate (local error O(dt^2), global error O(dt)) and is *not*
symplectic: it does not preserve the phase-space structure of the true dynamics, so orbital
energy drifts systematically (typically growing) with every step regardless of how small `dt`
is made, only more slowly. It is included mainly as a baseline for comparison and for very
short, non-orbital demonstrations; it is a poor choice for anything that needs to stay bound
over many periods.

### Symplectic (semi-implicit) Euler -- order 1, symplectic

A minimal change to forward Euler fixes its secular drift: update velocity first using the
current acceleration, then update position using the *already-updated* velocity.

```
a  = accel(x)
v_new = v + a * dt
x_new = x + v_new * dt
```

This single reordering makes the method symplectic: it exactly preserves a *shadow*
Hamiltonian close to the true one, so energy oscillates around its initial value rather than
drifting away. It remains first-order accurate, but its long-term qualitative behavior for
orbital mechanics is far better than plain Euler's.

### Leapfrog (velocity Verlet, kick-drift-kick) -- order 2, symplectic

`orbit`'s leapfrog is the kick-drift-kick form of velocity Verlet:

```
a0 = accel(x)
v_half = v + a0 * dt/2          (kick)
x_new  = x + v_half * dt        (drift)
a1 = accel(x_new)
v_new  = v_half + a1 * dt/2     (kick)
```

Leapfrog is second-order accurate and symplectic, and it is time-reversible: running it
backwards from the end state exactly retraces the forward trajectory (up to floating-point
rounding). Its symmetry (kick-drift-kick, split evenly around the midpoint) is what buys the
extra order over symplectic Euler at essentially the same cost -- two acceleration evaluations
per step compared to symplectic Euler's one, still far cheaper than RK4's four. For long
integrations of bound orbital systems, leapfrog is the default integrator in `orbit`
(`config_defaults` sets `integrator = "leapfrog"`) because it combines good accuracy, low cost,
and bounded energy error over arbitrarily long runs.

### Classical RK4 -- order 4, non-symplectic

Standard fourth-order Runge-Kutta applied to the combined position/velocity state of every
body. Four acceleration evaluations per step, each at a different intermediate state, combined
with the classic 1-2-2-1 weighted average:

```
k1 = f(x, v)
k2 = f(x + k1_x*dt/2, v + k1_v*dt/2)
k3 = f(x + k2_x*dt/2, v + k2_v*dt/2)
k4 = f(x + k3_x*dt,   v + k3_v*dt)
x_new, v_new = x, v + (dt/6) * (k1 + 2*k2 + 2*k3 + k4)
```

RK4 has local error O(dt^5) and global error O(dt^4) -- for smooth, short-to-medium
integrations at a fixed `dt`, RK4 is typically far more accurate than the second-order methods.
However, it is *not* symplectic: over very long integrations (many orbital periods) its energy
error, while small per step, is not bounded the way leapfrog's is, and can accumulate a slow
secular drift. RK4 needs scratch buffers for the four stage evaluations, allocated and freed
within `integrator_step`.

### Yoshida4 -- order 4, symplectic

Yoshida's fourth-order method composes leapfrog's second-order kick-drift-kick update four
times with carefully chosen sub-step weights, canceling the leading-order error term while
remaining exactly symplectic. The weights used are:

```
w1 = 1 / (2 - 2^(1/3))
w0 = -2^(1/3) * w1
```

and the step applies leapfrog sub-steps of length `w1*dt`, `w0*dt`, `w1*dt`, `w1*dt` in a
symmetric composition (the classic Yoshida/Suzuki triple-jump construction), summing to `dt`
overall. Because each sub-step is itself a symplectic leapfrog update, the composed method
inherits symplecticity and time-reversibility while achieving fourth-order accuracy -- the best
of both worlds, at the cost of four acceleration evaluations per step (same as RK4) but with
leapfrog's bounded long-term energy behavior instead of RK4's slow drift. Yoshida4 is the
right choice when both high accuracy per step and stability over thousands of periods matter,
such as long-term solar-system integrations.

### Comparison table

| Integrator          | Order | Symplectic | Accel evals/step | Energy behavior (long runs)        | Good for |
|----------------------|:-----:|:----------:|:-----------------:|-------------------------------------|----------|
| Euler                 | 1     | no         | 1                  | drifts monotonically                | quick sanity checks only |
| Symplectic Euler       | 1     | yes        | 1                  | bounded oscillation                 | cheap, short/rough orbital runs |
| Leapfrog (Verlet)      | 2     | yes        | 2                  | bounded oscillation, small amplitude| default; most orbital work |
| RK4                    | 4     | no         | 4                  | small per-step error, slow secular drift over very long runs | smooth short/medium integrations, high per-step accuracy |
| Yoshida4               | 4     | yes        | 4                  | bounded oscillation, tiny amplitude | long-term high-accuracy integrations |

The general lesson: a symplectic method's energy error does not grow without bound as
integration time increases -- it oscillates around the true value with an amplitude set by
`dt` and the method's order -- while a non-symplectic method's error can accumulate a
one-directional secular trend. This is why leapfrog and Yoshida4, despite lower or equal
per-step accuracy compared to RK4, are usually preferred for simulating orbital mechanics over
many periods.

## 4. Keplerian orbital elements and Kepler's equation

For any two bodies interacting only under mutual gravity (the two-body problem, with the
smaller body's mass often negligible next to the central body, using standard gravitational
parameter `mu = G * (m1 + m2)`), the relative orbit is a conic section fully described by six
classical (Keplerian) elements:

- `a` -- semi-major axis (m): half the long axis of the ellipse (or the analogous parameter,
  negative, for a hyperbola).
- `e` -- eccentricity (dimensionless): `0` is circular, `0 < e < 1` is elliptical, `e = 1` is
  parabolic, `e > 1` is hyperbolic.
- `i` -- inclination (rad): tilt of the orbital plane relative to a reference plane.
- `raan` -- right ascension of the ascending node (rad): where the orbit crosses the reference
  plane going "upward", measured from a reference direction.
- `argp` -- argument of periapsis (rad): angle from the ascending node to periapsis, measured
  in the orbital plane.
- `nu` -- true anomaly (rad): the body's current angular position measured from periapsis.

`orbit_from_state` converts a Cartesian state (position `r`, velocity `v`, both relative to the
central body) into these elements using the standard sequence: compute the specific angular
momentum vector `h = r x v` (which is normal to the orbital plane and gives `i` and the node
direction), the eccentricity vector `e_vec = (v x h)/mu - r/|r|` (which points toward periapsis
and has magnitude `e`), and the specific orbital energy to get `a`. For near-circular orbits
(`e < 1e-10`) periapsis is undefined, and for near-equatorial orbits (`i < 1e-10`) the
ascending node is undefined; in both cases `orbit_from_state` sets the corresponding angle(s)
to zero and folds the missing information into `nu` so the state is still round-trippable.
`orbit_to_state` is the inverse transform, placing the body in the orbital plane using `nu` and
then rotating by `argp`, `i`, and `raan` in turn.

### Kepler's equation

True anomaly `nu` describes *where* a body is, but not simply *when* -- a body moves faster
near periapsis than apoapsis, so angle does not advance uniformly in time. The standard
intermediate variable is the eccentric angle `E` (eccentric anomaly), related to the mean
anomaly `M` (which *does* advance uniformly with time: `M = M0 + n*(t - t0)`, where the mean
motion `n = sqrt(mu / a^3)`) by Kepler's equation:

```
M = E - e * sin(E)
```

This is transcendental in `E` and has no closed-form solution, so `orbit_kepler_E` solves it
numerically with Newton's iteration:

```
E_{k+1} = E_k - (E_k - e*sin(E_k) - M) / (1 - e*cos(E_k))
```

starting from `E_0 = M` (a good starting guess for low-to-moderate eccentricity), iterating
until the correction is below `1e-14`. Once `E` is known, the true anomaly follows from the
standard half-angle relation between `E` and `nu` (`orbit_true_from_mean` combines both
steps). Round-trip conversions through this pipeline (elements -> state -> elements, or mean
anomaly -> true anomaly -> mean anomaly) are held to better than `1e-8` relative error.

### Orbital period and vis-viva

The orbital period follows directly from Kepler's third law:

```
T = 2 * pi * sqrt(a^3 / mu)
```

(`orbit_period` returns `0` for non-elliptical or degenerate `a <= 0`, since hyperbolic and
parabolic orbits are not periodic).

The vis-viva equation gives instantaneous orbital speed at any radius `r` without needing the
full state:

```
v = sqrt(mu * (2/r - 1/a))
```

which is used, among other places, to compute the circular orbital speed of scenario bodies
(`v = sqrt(mu/r)` when `a = r`) and the speeds at either end of a Hohmann transfer.

## 5. Hohmann transfer

A Hohmann transfer is the minimum-delta-v two-impulse maneuver between two coplanar circular
orbits of radii `r1` (initial) and `r2` (final), around a body with gravitational parameter
`mu`. It works by putting the spacecraft on an elliptical transfer orbit whose periapsis
touches the inner circle and whose apoapsis touches the outer circle, then circularizing at
arrival.

**Transfer orbit.** The transfer ellipse has periapsis `r1` and apoapsis `r2` (or vice versa
for an inward transfer), so its semi-major axis is the average:

```
a_transfer = (r1 + r2) / 2
```

**First burn (at r1).** Before the burn the spacecraft is in a circular orbit of radius `r1`
with speed `v_circ1 = sqrt(mu / r1)`. The burn must raise it onto the transfer ellipse, whose
speed at radius `r1` (using vis-viva with `a = a_transfer`) is
`v_transfer1 = sqrt(mu * (2/r1 - 1/a_transfer))`. The required impulse is the difference:

```
dv1 = v_transfer1 - v_circ1
```

**Coast.** The spacecraft coasts along the transfer ellipse from periapsis to apoapsis -- half
the ellipse's period:

```
tof = pi * sqrt(a_transfer^3 / mu)
```

(`hohmann_plan`'s `tof` field: this is half of `orbit_period(a_transfer, mu)`, since the
transfer only covers half the ellipse.)

**Second burn (at r2).** On arrival the spacecraft is still on the transfer ellipse, now at its
apoapsis, moving at `v_transfer2 = sqrt(mu * (2/r2 - 1/a_transfer))`. To circularize it must
match the destination's circular speed `v_circ2 = sqrt(mu / r2)`:

```
dv2 = v_circ2 - v_transfer2
```

**Total cost.** The mission's delta-v budget for the maneuver is `dv1 + dv2`, which for
outward transfers (`r2 > r1`) is minimized among all two-impulse coplanar transfers between the
two circles -- the reason the Hohmann transfer is the standard baseline for orbital
maneuvering. `orbit`'s `spacecraft.h` API represents the whole mission as a `flight_plan`: a
sorted list of `burn` events (`t`, `dv`, target `body_id`), applied to the simulation as the
clock passes each burn's time (`flight_plan_apply`), with the total commanded delta-v available
via `spacecraft_delta_v_budget`.

## 6. Collisions: perfectly inelastic merging

When two bodies' separation drops below the sum of their radii (`dist < r_i + r_j`),
`collision_merge` treats the encounter as a perfectly inelastic collision and merges them into
one body. Two conservation laws determine the result:

**Mass** is simply additive:

```
m_new = m_i + m_j
```

**Momentum** is conserved, which (combined with the merged mass) fixes the merged velocity as
the mass-weighted average of the two incoming velocities:

```
v_new = (m_i * v_i + m_j * v_j) / m_new
```

(this falls directly out of `p_new = p_i + p_j` and `p = m*v`). Position is likewise set to the
mass-weighted mean of the two positions, which keeps the system's center of mass unchanged by
the merge:

```
pos_new = (m_i * pos_i + m_j * pos_j) / m_new
```

**Radius** is not separately conserved by any physical law here; `orbit` assumes constant,
equal density before and after the merge, so volume (proportional to `r^3`) adds:

```
r_new = cbrt(r_i^3 + r_j^3)
```

Kinetic energy is *not* conserved in an inelastic collision (some is lost, notionally to heat
and deformation) -- this is what "inelastic" means, and it is why total mechanical energy
(`diag.total`) is expected to drop, not stay constant, across a run with collisions enabled.

The heavier of the two bodies survives (kept name, kind, color, and id), the lighter one is
marked `alive = 0` and left in place until the world is compacted. Because a merge can create a
body large enough to immediately overlap a third, `collision_merge` repeats its pass over all
pairs until no overlaps remain, correctly handling chains (A merges with B, and the result then
merges with C). For large body counts (`n > 64`) it uses a spatial hash grid (cell size `2 *`
the largest radius present) to avoid the O(n^2) pairwise distance checks; for smaller `n` it
falls back to direct O(n^2) pairwise checks, which is faster in practice at that scale.

## 7. Diagnostics: energy, momentum, and the virial ratio

`diagnostics_compute` fills a `diag` struct every time it is called, used both for live status
output and for checking a run's physical correctness in tests.

**Kinetic energy** is the standard sum over alive bodies:

```
K = sum_i  (1/2) * m_i * |v_i|^2
```

**Potential energy** uses the same softened pairwise formula as `gravity_potential`, computed
independently (its own O(n^2) loop) so the two can be cross-checked against each other:

```
U = - sum_{i<j}  G * m_i * m_j / sqrt(r_ij^2 + eps^2)
```

**Total energy** is `K + U`. For an isolated system (no external forces) with a
non-dissipative integrator, `total` should stay constant over time except for numerical
integration error; watching `total` is the standard way to validate both an integrator and a
choice of `dt`.

**Momentum** `sum_i m_i * v_i` and **angular momentum** `sum_i m_i * (r_i x v_i)` (about the
origin) are both exactly conserved for an isolated system under Newtonian gravity, regardless
of integrator, because gravity is an internal force (Newton's third law makes every pairwise
contribution cancel in the sum) -- these are strong, integrator-independent checks: any nonzero
drift in momentum or angular momentum almost always indicates a bug rather than acceptable
numerical error.

**Center of mass** `com = (sum_i m_i * r_i) / (sum_i m_i)` should move at constant velocity
(uniform, since momentum is conserved) for an isolated system; `world_recenter` uses this to
place a scenario's center of mass at the origin, moving with it removed, at setup time.

**Drift.** `diagnostics_rel_drift(e0, e)` reports how far a quantity (typically total energy)
has moved from its initial value `e0`, as a relative fraction:

```
rel_drift = |e - e0| / max(|e0|, 1e-300)
```

The `max` with a tiny floor avoids division by zero when `e0` happens to be exactly zero (for
example a marginally bound or perfectly balanced initial configuration). A well-behaved,
symplectic integrator run at a reasonable `dt` should show `rel_drift` staying small and
bounded (not growing without limit) over the whole simulation; a growing `rel_drift` over time
signals either a non-symplectic integrator accumulating secular error, too large a `dt`, or an
actual bug.

**Virial ratio.** For a bound, statistically relaxed self-gravitating system in equilibrium,
the virial theorem states `2*K + U = 0`, i.e. `2*K = -U` (since `U` is negative for a bound
system). `diag.virial_ratio` reports this as `2*K / |U|`: a value near `1.0` indicates the
system is close to virial equilibrium (neither systematically collapsing nor expanding on
average), while a value well above or below `1.0` indicates the system is out of equilibrium --
kinetic-energy-dominated (expanding/unbound tendency) if greater than 1, potential-energy-
dominated (collapsing tendency) if less than 1. This is used, for example, to check that the
`cluster` scenario's Plummer sphere is set up close to equilibrium.

## 8. N-body units (G = 1)

Simulating real solar-system dynamics in SI units means very large numbers (masses around
`1e24`-`1e30` kg, distances around `1e11` m) multiplying a very small `G`
(`6.674e-11 m^3 kg^-1 s^-2`), which is numerically inconvenient and makes cross-scenario
comparisons (a galaxy simulation vs. a spacecraft maneuver) harder to reason about. For
scenarios that don't correspond to real physical bodies -- `figure8`, `cluster`, and `disk` --
`orbit` instead uses **N-body units**, in which the world sets `w->G = 1.0` and masses,
positions, and velocities are chosen in a consistent, dimensionless system rather than
kilograms, meters, and seconds.

The physics is identical either way: every formula in this document (gravity, softening, the
integrators, diagnostics, Kepler's equation) is written in terms of `G`, `mu`, masses,
distances and times without assuming any particular unit system, so setting `G = 1` and
choosing "natural" unit masses (e.g. total system mass `= 1`) and length scales (e.g. scale
radius `= 1`) is simply a convenient rescaling, not a different set of equations. This is the
same convention used throughout computational astrophysics for idealized N-body problems: the
classic `figure8` choreography and Plummer-sphere cluster initial conditions are both
traditionally quoted in `G = 1` units, which is why `orbit`'s `scenario_load` sets `w->G = 1.0`
for those scenarios while the physically-scaled scenarios (`solar`, `earth-moon`, `binary`,
`hohmann`) use real SI values throughout (`G = 6.674e-11`, masses in kg, distances in meters).
Code that only uses `w->G` and never hardcodes a specific value of `G` (as every module in
`orbit` does) works correctly in both unit systems without modification.
