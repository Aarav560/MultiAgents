/* output_csv.h - CSV output for world snapshots. */
#ifndef ORBIT_OUTPUT_CSV_H
#define ORBIT_OUTPUT_CSV_H

#include "world.h"

typedef struct csv_writer csv_writer;

/* Opens a CSV file for writing. Writes header: step,t,id,name,kind,mass,x,y,z,vx,vy,vz.
   Returns NULL on failure. */
csv_writer *csv_open(const char *path);

/* Writes one row per alive body to the CSV file. Returns 0 on success, -1 on error. */
int csv_write_frame(csv_writer *c, const world *w);

/* Returns the number of data rows written (not counting the header). */
long csv_rows(const csv_writer *c);

/* Flushes, closes, and frees the csv_writer. Returns fclose status (0 on success). */
int csv_close(csv_writer *c);

#endif
