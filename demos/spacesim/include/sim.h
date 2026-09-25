/* sim.h - integration layer: runs a configured scenario end to end, writing outputs. */
#ifndef ORBIT_SIM_H
#define ORBIT_SIM_H

#include "config.h"
#include "world.h"
#include <stddef.h>
#include <stdio.h>

typedef struct simulation simulation;

/* Builds a simulation from cfg (scenario, gravity, integrator, outputs).
   Returns NULL and writes a message into err (if non-NULL) on failure. */
simulation *sim_create(const sim_config *cfg, char *err, size_t errlen);
/* Runs to completion, writing outputs; status lines and the summary go to log (NULL = silent).
   Returns 0, or -1 on an integration or output error. */
int sim_run(simulation *s, FILE *log);
void sim_destroy(simulation *s);
const world *sim_world(const simulation *s);

#endif
