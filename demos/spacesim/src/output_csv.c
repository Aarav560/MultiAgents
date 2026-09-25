#include "output_csv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct csv_writer {
    FILE *f;
    long rows;
};

csv_writer *csv_open(const char *path) {
    csv_writer *c = malloc(sizeof *c);
    if (!c) return NULL;

    c->f = fopen(path, "w");
    if (!c->f) {
        free(c);
        return NULL;
    }

    c->rows = 0;

    /* Write header */
    if (fprintf(c->f, "step,t,id,name,kind,mass,x,y,z,vx,vy,vz\n") < 0) {
        fclose(c->f);
        free(c);
        return NULL;
    }

    return c;
}

/* RFC 4180: escape a field that may contain commas, quotes, or newlines. */
static int csv_write_field(FILE *f, const char *field) {
    int needs_quote = 0;

    /* Check if field needs quoting */
    for (const char *p = field; *p; p++) {
        if (*p == ',' || *p == '"' || *p == '\n' || *p == '\r') {
            needs_quote = 1;
            break;
        }
    }

    if (!needs_quote) {
        /* No special characters, write as-is */
        return fprintf(f, "%s", field) >= 0 ? 0 : -1;
    }

    /* Needs quoting: wrap in quotes and double any internal quotes */
    if (fputc('"', f) == EOF) return -1;
    for (const char *p = field; *p; p++) {
        if (*p == '"') {
            if (fputc('"', f) == EOF) return -1;
            if (fputc('"', f) == EOF) return -1;
        } else {
            if (fputc(*p, f) == EOF) return -1;
        }
    }
    if (fputc('"', f) == EOF) return -1;
    return 0;
}

int csv_write_frame(csv_writer *c, const world *w) {
    if (!c || !c->f) return -1;

    for (int i = 0; i < w->count; i++) {
        const body *b = &w->bodies[i];

        /* Skip dead bodies */
        if (!b->alive) continue;

        /* Write step */
        if (fprintf(c->f, "%llu", w->step) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        /* Write t */
        if (fprintf(c->f, "%.17g", w->t) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        /* Write id */
        if (fprintf(c->f, "%d", b->id) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        /* Write name (with escaping) */
        if (csv_write_field(c->f, b->name) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        /* Write kind */
        if (fprintf(c->f, "%s", body_kind_name(b->kind)) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        /* Write mass */
        if (fprintf(c->f, "%.17g", b->mass) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        /* Write position */
        if (fprintf(c->f, "%.17g", b->pos.x) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        if (fprintf(c->f, "%.17g", b->pos.y) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        if (fprintf(c->f, "%.17g", b->pos.z) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        /* Write velocity */
        if (fprintf(c->f, "%.17g", b->vel.x) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        if (fprintf(c->f, "%.17g", b->vel.y) < 0) return -1;
        if (fputc(',', c->f) == EOF) return -1;

        if (fprintf(c->f, "%.17g", b->vel.z) < 0) return -1;

        /* End of line */
        if (fputc('\n', c->f) == EOF) return -1;

        c->rows++;
    }

    return 0;
}

long csv_rows(const csv_writer *c) {
    return c ? c->rows : 0;
}

int csv_close(csv_writer *c) {
    if (!c) return -1;
    int ret = fclose(c->f);
    free(c);
    return ret;
}
