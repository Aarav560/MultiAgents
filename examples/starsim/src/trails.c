#include "trails.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    int id;      /* body id (-1 if slot unused) */
    int start;   /* starting index in positions array */
    int count;   /* current count (0 to max_points) */
    int next;    /* next write index in ring buffer (0 to max_points-1) */
} slot_t;

struct trails {
    vec2 *positions;   /* flat array of all positions */
    slot_t *slots;     /* linear-probe hash table */
    int max_bodies;
    int max_points;
    int num_slots;     /* size of hash table (power of 2) */
    int slot_mask;     /* num_slots - 1 */
    int num_used;      /* how many slots are currently in use */
};

/* Linear-probe hash: find slot for id, return index (may be empty) */
static int hash_find(const slot_t *slots, int mask, int id) {
    int idx = id & mask;
    while (slots[idx].id != -1 && slots[idx].id != id) {
        idx = (idx + 1) & mask;
    }
    return idx;
}

trails *trails_create(int max_bodies, int max_points) {
    if (max_bodies < 1 || max_points < 1) return NULL;

    trails *t = malloc(sizeof *t);
    if (!t) return NULL;

    /* Allocate flat position array */
    t->positions = malloc((size_t)max_bodies * max_points * sizeof(vec2));
    if (!t->positions) {
        free(t);
        return NULL;
    }

    /* Allocate hash table (power of 2, at least 2*max_bodies) */
    int num_slots = 16;
    while (num_slots < 2 * max_bodies) num_slots *= 2;

    t->slots = malloc((size_t)num_slots * sizeof(slot_t));
    if (!t->slots) {
        free(t->positions);
        free(t);
        return NULL;
    }

    /* Initialize all slots as empty */
    for (int i = 0; i < num_slots; i++) {
        t->slots[i].id = -1;
    }

    t->max_bodies = max_bodies;
    t->max_points = max_points;
    t->num_slots = num_slots;
    t->slot_mask = num_slots - 1;
    t->num_used = 0;

    return t;
}

void trails_free(trails *t) {
    if (!t) return;
    free(t->positions);
    free(t->slots);
    free(t);
}

void trails_clear(trails *t) {
    if (!t) return;
    for (int i = 0; i < t->num_slots; i++) {
        t->slots[i].id = -1;
    }
    t->num_used = 0;
}

void trails_record(trails *t, const world *w) {
    if (!t || !w) return;

    /* First pass: record positions for alive non-dust bodies */
    for (int i = 0; i < w->n; i++) {
        if (!w->b[i].alive || w->b[i].kind == KIND_DUST) continue;

        int id = w->b[i].id;
        int slot_idx = hash_find(t->slots, t->slot_mask, id);

        if (t->slots[slot_idx].id == -1) {
            /* Not found - allocate a new slot if we have room */
            if (t->num_used >= t->max_bodies) {
                continue; /* Ignore - no more slots available */
            }

            /* Initialize new slot */
            t->slots[slot_idx].id = id;
            t->slots[slot_idx].start = t->num_used * t->max_points;
            t->slots[slot_idx].count = 0;
            t->slots[slot_idx].next = 0;
            t->num_used++;
        }

        /* Append position to this body's trail */
        slot_t *slot = &t->slots[slot_idx];
        int idx = slot->start + slot->next;
        t->positions[idx] = w->b[i].pos;

        if (slot->count < t->max_points) {
            slot->count++;
        }
        slot->next = (slot->next + 1) % t->max_points;
    }

    /* Second pass: release slots for dead bodies */
    for (int i = 0; i < t->num_slots; i++) {
        if (t->slots[i].id != -1) {
            if (world_index_of(w, t->slots[i].id) == -1) {
                /* Body is no longer alive - release the slot */
                t->slots[i].id = -1;
            }
        }
    }
}

int trails_get(const trails *t, int id, vec2 *out, int max_out) {
    if (!t || !out || max_out < 1) return 0;

    int slot_idx = hash_find(t->slots, t->slot_mask, id);
    if (t->slots[slot_idx].id != id) {
        return 0; /* Not found */
    }

    slot_t *slot = &t->slots[slot_idx];
    int to_copy = slot->count < max_out ? slot->count : max_out;

    /* Copy oldest-first from ring buffer */
    int start_offset;
    if (slot->count >= t->max_points) {
        /* Ring has wrapped: next points to the oldest */
        start_offset = slot->next;
    } else {
        /* Ring hasn't wrapped yet: oldest is at 0 */
        start_offset = 0;
    }

    for (int i = 0; i < to_copy; i++) {
        int offset = (start_offset + i) % t->max_points;
        out[i] = t->positions[slot->start + offset];
    }

    return to_copy;
}
