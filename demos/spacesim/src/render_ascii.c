/* render_ascii.c - terminal renderer implementation. */
#include "render_ascii.h"
#include "vec3.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

typedef struct {
    char glyph;
    double mass;
} cell_content;

int render_ascii(const world *w, char *buf, size_t buflen, int cols, int rows, double scale) {
    /* Check buffer size: rows lines of cols chars + newline each + NUL terminator */
    size_t required = (size_t)rows * (cols + 1) + 1;
    if (buflen < required) {
        return -1;
    }

    /* Validate dimensions */
    if (cols < 3 || rows < 3) {
        return -1;
    }

    /* Calculate effective scale */
    double effective_scale = scale;
    if (effective_scale == 0.0) {
        double extent = world_extent(w);
        effective_scale = (extent * 1.1) / ((cols - 2) / 2.0);
        if (effective_scale <= 0.0) {
            effective_scale = 1.0;
        }
    }

    /* Get center of mass */
    vec3 com = world_com(w);

    /* Initialize grid: rows x cols interior cells */
    cell_content *grid = (cell_content *)malloc((size_t)rows * (size_t)cols * sizeof(cell_content));
    if (!grid) {
        return -1;
    }

    /* Fill grid with spaces (empty cells) */
    for (int i = 0; i < rows * cols; i++) {
        grid[i].glyph = ' ';
        grid[i].mass = 0.0;
    }

    /* Draw border */
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            if (r == 0 || r == rows - 1) {
                if (c == 0 || c == cols - 1) {
                    grid[r * cols + c].glyph = '+';
                } else {
                    grid[r * cols + c].glyph = '-';
                }
            } else {
                if (c == 0 || c == cols - 1) {
                    grid[r * cols + c].glyph = '|';
                }
            }
        }
    }

    /* Place bodies */
    for (int i = 0; i < w->count; i++) {
        if (!w->bodies[i].alive) {
            continue;
        }

        body *b = &w->bodies[i];

        /* Project position relative to COM onto x-y plane */
        vec3 rel_pos = vec3_sub(b->pos, com);

        /* Convert to grid coordinates (center is at (cols-1)/2, (rows-1)/2) */
        double grid_x = rel_pos.x / effective_scale;
        double grid_y = rel_pos.y / effective_scale;

        /* Grid origin at center, using integer division for center */
        int center_col = (cols - 1) / 2;
        int center_row = (rows - 1) / 2;
        int cx = center_col + (int)floor(grid_x + 0.5);
        int cy = center_row - (int)floor(grid_y + 0.5); /* y-axis is inverted in display */

        /* Check if within interior bounds (not on border) */
        if (cx > 0 && cx < cols - 1 && cy > 0 && cy < rows - 1) {
            int idx = cy * cols + cx;
            char glyph = ' ';

            /* Determine glyph based on body kind */
            switch (b->kind) {
                case BODY_STAR:
                    glyph = '*';
                    break;
                case BODY_PLANET:
                    glyph = 'O';
                    break;
                case BODY_MOON:
                    glyph = 'o';
                    break;
                case BODY_ASTEROID:
                    glyph = ':';
                    break;
                case BODY_SPACECRAFT:
                    glyph = 'A';
                    break;
                case BODY_PARTICLE:
                    glyph = '.';
                    break;
                default:
                    glyph = '?';
                    break;
            }

            /* Place body if heavier than current occupant */
            if (b->mass > grid[idx].mass) {
                grid[idx].glyph = glyph;
                grid[idx].mass = b->mass;
            }
        }
    }

    /* Format output into buffer */
    char *p = buf;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            *p++ = grid[r * cols + c].glyph;
        }
        *p++ = '\n';
    }
    *p = '\0';

    free(grid);
    return 0;
}
