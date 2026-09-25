# Physics of `orbit`

This document explains the physics and numerical methods behind `orbit`: gravity and
softening, the two gravity solvers, the five integrators, Keplerian mechanics, the Hohmann
transfer, collisions, the diagnostics, and N-body units. All math is plain arithmetic, no
LaTeX.

## Newtonian gravity and softening

Every body attracts every other under Newton's law. The acceleration on body `i` from `j` is

```
a_i += G * m_j * (r_j - r_i) / |r_j - r_i|^3
```

summed over all other alive bodies. If two bodies pass very close, `|r_j - r_i|` approaches
zero and the force diverges, breaking energy conservation and producing unphysical slingshots.
`orbit` fixes this with a softening length `eps` (`world.softening`):

```
a_i = sum_j  G * m_j * (r_j - r_i) / (r^2 + eps^2)^(3/2)
```

As `r -> 0` the acceleration saturates instead of blowing up. Softening amounts to smearing
each point mass into a small sphere of radius roughly `eps`. Real solar-system scenarios use a
tiny or zero `eps`; loosely sampled scenarios (`cluster`, `disk`) need `eps` around the mean
particle spacing, or close encounters dominate with spurious kicks. `gravity_potential` and
`diagnostics_compute` use the same softened denominator, `-G*m_i*m_j / sqrt(r^2 + eps^2)`
summed over each pair once, so energy checks are consistent with the force actually integrated.

## Direct summation vs. Barnes-Hut

`gravity_direct` sums every pair once (Newton's third law halves the work) for an exact
O(n^2) force. It is simple and exact, and is the right choice up to a few thousand bodies or
whenever exactness matters more than speed (e.g. validating an integrator).

For larger `n`, Barnes-Hut (`octree.h`) groups distant bodies and treats a cluster as one point
mass at its center of mass. The domain is a recursively subdivided octree: a cube splits into 8
child octants when it holds more than one body, and every internal node caches the aggregate
mass and center of mass of everything beneath it. To evaluate the acceleration at a point, the
tree is walked from the root, and at each node the **opening criterion**

```
s / d < theta
```

(`s` = node cube size, `d` = distance to the node's center of mass, `theta` = opening angle,
typically 0.3-0.8) decides whether to use the node's aggregate mass directly or recurse into its
children. Leaves are always evaluated exactly. Building and evaluating the tree both cost
O(n log n) on average, since each insertion or query touches O(log n) levels -- the payoff over
O(n^2) once `n` reaches the thousands.

`theta` trades accuracy for speed: `theta = 0` opens every node down to leaves and reproduces
`gravity_direct` to within 1e-9 relative error (the correctness check in the octree test); small
theta (0.2-0.3) stays close to exact at less speedup; large theta (0.7-1.0) accepts more
distant approximation, showing up as energy and angular-momentum drift over long runs. 0.5 is a
typical default. Because bodies can coincide, tree depth is capped at 64, with the deepest leaf
holding several bodies rather than subdividing forever.

## The integrators

All five integrators advance `world.t` by `dt`, increment `world.step`, and call the supplied
`accel_fn` themselves rather than trusting stale acceleration.

**Euler** (order 1, non-symplectic): `v_new = v + a*dt`, `x_new = x + v*dt`, using acceleration
evaluated once at the start. Simple but *not* symplectic: energy drifts systematically with
every step no matter how small `dt` is. Useful only as a baseline.

**Symplectic Euler** (order 1, symplectic): update velocity first, then position with the
*new* velocity: `v_new = v + a*dt`, `x_new = x + v_new*dt`. This reordering alone makes the
method symplectic -- it preserves a shadow Hamiltonian close to the true one, so energy
oscillates around its initial value instead of drifting away.

**Leapfrog / velocity Verlet** (order 2, symplectic), kick-drift-kick:

```
v_half = v + a(x)*dt/2
x_new  = x + v_half*dt
v_new  = v_half + a(x_new)*dt/2
```

Second-order accurate, symplectic, and time-reversible. Its symmetric split buys an extra order
over symplectic Euler for one more acceleration evaluation (2 vs 1), far cheaper than RK4's 4.
It is `orbit`'s default integrator for exactly this reason: good accuracy, low cost, and bounded
energy error over arbitrarily long runs.

**Classical RK4** (order 4, non-symplectic): the standard four-stage Runge-Kutta applied to
combined position/velocity state, with the 1-2-2-1 weighted average of four acceleration
evaluations at different intermediate states. Global error O(dt^4) -- very accurate per step for
smooth, short-to-medium integrations -- but not symplectic, so over very long runs (many
periods) it can accumulate a slow secular energy drift even though each step's error is small.
Needs per-call scratch buffers for the four stages.

**Yoshida4** (order 4, symplectic): composes leapfrog's kick-drift-kick update four times with
weights

```
w1 = 1 / (2 - 2^(1/3)),   w0 = -2^(1/3) * w1
```

in a symmetric triple-jump sequence (sub-steps `w1*dt, w0*dt, w1*dt`, appropriately combined) that
cancels leapfrog's leading error term while remaining exactly symplectic. Fourth-order accuracy
with leapfrog's bounded long-term energy behavior, at the same cost as RK4 (4 accel evals/step).
The right choice when both per-step accuracy and stability over thousands of periods matter.

| Integrator | Order | Symplectic | Accel evals/step | Long-run energy |
|---|:---:|:---:|:---:|---|
| Euler | 1 | no | 1 | drifts monotonically |
| Symplectic Euler | 1 | yes | 1 | bounded oscillation |
| Leapfrog | 2 | yes | 2 | bounded, small amplitude |
| RK4 | 4 | no | 4 | small per-step error, slow secular drift |
| Yoshida4 | 4 | yes | 4 | bounded, tiny amplitude |

A symplectic method's energy error oscillates around the true value with an amplitude set by
`dt` and order, instead of accumulating a one-directional trend -- why leapfrog and Yoshida4 are
generally preferred for long orbital integrations despite RK4's higher raw per-step accuracy.

## Keplerian elements and Kepler's equation

For two bodies under mutual gravity (gravitational parameter `mu = G*(m1+m2)`), the relative
orbit is a conic fully described by six elements: semi-major axis `a` (m), eccentricity `e`
(0 = circular, 0<e<1 ellipse, e>=1 parabola/hyperbola), inclination `i`, right ascension of the
ascending node `raan`, argument of periapsis `argp`, and true anomaly `nu` (all radians).
`orbit_from_state` derives them from a Cartesian state via the specific angular momentum
`h = r x v` (giving `i` and the node) and the eccentricity vector
`e_vec = (v x h)/mu - r/|r|` (giving `e` and periapsis direction). For near-circular
(`e < 1e-10`) or near-equatorial (`i < 1e-10`) orbits the undefined angles are set to zero and
folded into `nu`, keeping the state round-trippable to better than 1e-8 relative error.
`orbit_to_state` is the inverse: place the body using `nu` in the orbital plane, then rotate by
`argp`, `i`, `raan`.

True anomaly gives position but not time, since angular speed varies around the orbit. The mean
anomaly `M` advances uniformly (`M = M0 + n*(t-t0)`, mean motion `n = sqrt(mu/a^3)`) and relates
to the eccentric anomaly `E` by **Kepler's equation**:

```
M = E - e*sin(E)
```

transcendental in `E`, solved by `orbit_kepler_E` with Newton iteration from `E0 = M`:

```
E_{k+1} = E_k - (E_k - e*sin(E_k) - M) / (1 - e*cos(E_k))
```

to 1e-14. `E` then converts to true anomaly by the standard half-angle relation
(`orbit_true_from_mean`). Period follows from Kepler's third law,
`T = 2*pi*sqrt(a^3/mu)` (zero for non-elliptical `a <= 0`), and instantaneous speed at radius
`r` from vis-viva, `v = sqrt(mu*(2/r - 1/a))`.

## Hohmann transfer

A Hohmann transfer is the minimum-delta-v two-impulse maneuver between circular orbits of radii
`r1` and `r2` around a body of parameter `mu`, via an elliptical transfer orbit with periapsis
`r1` and apoapsis `r2`, so `a_transfer = (r1+r2)/2`.

- **First burn** at `r1`: raise the circular speed `v_circ1 = sqrt(mu/r1)` to the transfer
  ellipse's speed there, `v_t1 = sqrt(mu*(2/r1 - 1/a_transfer))`: `dv1 = v_t1 - v_circ1`.
- **Coast** half the transfer ellipse's period: `tof = pi*sqrt(a_transfer^3/mu)`.
- **Second burn** at `r2`, arriving at the ellipse's apoapsis speed
  `v_t2 = sqrt(mu*(2/r2 - 1/a_transfer))`, circularizing to `v_circ2 = sqrt(mu/r2)`:
  `dv2 = v_circ2 - v_t2`.

Total cost `dv1 + dv2` is minimal among two-impulse coplanar transfers for `r2 > r1`, which is
why it is the standard baseline maneuver. `spacecraft.h` represents a mission as a
`flight_plan`: burns sorted by time, applied as the simulation clock passes each one
(`flight_plan_apply`), with total commanded delta-v via `spacecraft_delta_v_budget`.

## Perfectly inelastic collisions

When two bodies' separation drops below the sum of their radii, `collision_merge` merges them,
conserving mass and momentum:

```
m_new   = m_i + m_j
v_new   = (m_i*v_i + m_j*v_j) / m_new
pos_new = (m_i*pos_i + m_j*pos_j) / m_new
```

Radius is not separately conserved; assuming constant density, volume (proportional to `r^3`)
adds: `r_new = cbrt(r_i^3 + r_j^3)`. Kinetic energy is *not* conserved -- that is what
"inelastic" means -- so total mechanical energy is expected to drop across a run with
collisions enabled, not stay constant. The heavier body survives (name, kind, color, id); the
lighter one is marked dead. Because a merge can create overlaps with a third body, the pass
repeats until none remain, handling chains correctly. For `n > 64` a spatial hash grid (cell
size `2 *` the largest radius) avoids O(n^2) checks; smaller `n` uses direct O(n^2), which is
faster at that scale.

## Diagnostics: energy, momentum, virial ratio

`diagnostics_compute` reports kinetic energy `K = sum (1/2)*m*|v|^2`, potential energy `U`
(same softened formula as `gravity_potential`, computed independently so the two can be
cross-checked), and total `K+U`. For an isolated system, total energy should stay constant over
time except for integration error, making it the standard check on an integrator and `dt`
choice. Momentum `sum m*v` and angular momentum `sum m*(r x v)` are exactly conserved for any
isolated Newtonian system regardless of integrator (gravity is internal, so pairwise
contributions cancel) -- any drift in these almost always signals a bug rather than acceptable
numerical error. Center of mass `com = (sum m*r)/(sum m)` should move at constant velocity;
`world_recenter` places it at the origin, at rest, at setup.

`diagnostics_rel_drift(e0, e) = |e - e0| / max(|e0|, 1e-300)` reports how far a quantity (usually
total energy) has moved from its initial value, with a tiny floor to avoid dividing by zero. A
well-behaved symplectic run at a reasonable `dt` keeps this small and bounded over the whole
run; growth over time points to a non-symplectic integrator's secular error, too large a `dt`,
or a bug.

The virial theorem states that a bound, relaxed self-gravitating system in equilibrium satisfies
`2*K + U = 0`. `diag.virial_ratio = 2*K/|U|` reports this: near 1.0 means close to equilibrium;
above 1 indicates a kinetic-dominated (expanding) tendency, below 1 a potential-dominated
(collapsing) one. This checks, for example, that the `cluster` scenario's Plummer sphere starts
near equilibrium.

## N-body units (G = 1)

Real solar-system dynamics in SI units mixes very large numbers (masses ~1e24-1e30 kg,
distances ~1e11 m) with a tiny `G` (6.674e-11), which is numerically awkward. For scenarios
without real physical scale -- `figure8`, `cluster`, `disk` -- `orbit` instead sets `w->G = 1.0`
and chooses masses, distances and times in a consistent dimensionless system (e.g. total system
mass = 1, scale radius = 1). The physics is unchanged: every formula in this document is written
in terms of `G` and `mu` without assuming a specific unit system, so `G = 1` is simply a
convenient rescaling, matching the convention used throughout computational astrophysics for
idealized N-body problems (the `figure8` choreography and Plummer-sphere initial conditions are
traditionally quoted this way). Scenarios with real physical bodies (`solar`, `earth-moon`,
`binary`, `hohmann`) use SI values throughout. Because every module reads `w->G` rather than
hardcoding a constant, the same code works correctly in either unit system.
