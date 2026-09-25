#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(sim_config *c) {
    memset(c, 0, sizeof *c);
    strcpy(c->scenario, "solar");
    strcpy(c->integrator, "leapfrog");
    strcpy(c->gravity, "direct");
    c->theta = 0.5;
    c->fps = 20.0;
    c->dt = 0.0;
    c->duration = 0.0;
    c->steps = 0;
    c->output_every = 0;
    c->softening = -1.0;
    c->scale = 0.0;
    c->csv_path[0] = '\0';
    c->ppm_dir[0] = '\0';
    c->ascii = 0;
    c->width = 800;
    c->height = 800;
    c->trails = 1;
    c->collisions = 1;
    c->quiet = 0;
    c->seed = 42;
}

/* Parse a boolean from text. Accepts 1/0/true/false/yes/no/on/off.
   Returns 1 for true, 0 for false, -1 for invalid. */
static int parse_bool(const char *value) {
    if (strcmp(value, "1") == 0 || strcmp(value, "true") == 0 ||
        strcmp(value, "yes") == 0 || strcmp(value, "on") == 0) {
        return 1;
    }
    if (strcmp(value, "0") == 0 || strcmp(value, "false") == 0 ||
        strcmp(value, "no") == 0 || strcmp(value, "off") == 0) {
        return 0;
    }
    return -1;
}

/* Parse a double with full-string validation. */
static int parse_double(const char *value, double *out) {
    char *end;
    double x = strtod(value, &end);
    /* Ensure the entire string was consumed */
    if (end == value || *end != '\0') return -1;
    *out = x;
    return 0;
}

/* Parse a long with full-string validation. */
static int parse_long(const char *value, long *out) {
    char *end;
    long x = strtol(value, &end, 10);
    /* Ensure the entire string was consumed */
    if (end == value || *end != '\0') return -1;
    *out = x;
    return 0;
}

/* Parse an unsigned long long with full-string validation. */
static int parse_ull(const char *value, unsigned long long *out) {
    char *end;
    unsigned long long x = strtoull(value, &end, 10);
    /* Ensure the entire string was consumed */
    if (end == value || *end != '\0') return -1;
    *out = x;
    return 0;
}

int config_set(sim_config *c, const char *key, const char *value) {
    if (strcmp(key, "scenario") == 0) {
        if (strlen(value) >= 32) return -2;
        strcpy(c->scenario, value);
        return 0;
    }
    if (strcmp(key, "integrator") == 0) {
        if (strlen(value) >= 16) return -2;
        strcpy(c->integrator, value);
        return 0;
    }
    if (strcmp(key, "gravity") == 0) {
        if (strlen(value) >= 16) return -2;
        strcpy(c->gravity, value);
        return 0;
    }
    if (strcmp(key, "theta") == 0) {
        double x;
        if (parse_double(value, &x) != 0 || x < 0.0) return -2;
        c->theta = x;
        return 0;
    }
    if (strcmp(key, "dt") == 0) {
        double x;
        if (parse_double(value, &x) != 0 || x < 0.0) return -2;
        c->dt = x;
        return 0;
    }
    if (strcmp(key, "duration") == 0) {
        double x;
        if (parse_double(value, &x) != 0 || x < 0.0) return -2;
        c->duration = x;
        return 0;
    }
    if (strcmp(key, "steps") == 0) {
        long x;
        if (parse_long(value, &x) != 0 || x < 0) return -2;
        c->steps = x;
        return 0;
    }
    if (strcmp(key, "every") == 0) {
        long x;
        if (parse_long(value, &x) != 0 || x < 0) return -2;
        c->output_every = x;
        return 0;
    }
    if (strcmp(key, "softening") == 0) {
        double x;
        if (parse_double(value, &x) != 0) return -2;
        c->softening = x;
        return 0;
    }
    if (strcmp(key, "fps") == 0) {
        double x;
        if (parse_double(value, &x) != 0 || x < 0.0) return -2;
        c->fps = x;
        return 0;
    }
    if (strcmp(key, "scale") == 0) {
        double x;
        if (parse_double(value, &x) != 0 || x < 0.0) return -2;
        c->scale = x;
        return 0;
    }
    if (strcmp(key, "csv") == 0) {
        if (strlen(value) >= 256) return -2;
        strcpy(c->csv_path, value);
        return 0;
    }
    if (strcmp(key, "ppm") == 0) {
        if (strlen(value) >= 256) return -2;
        strcpy(c->ppm_dir, value);
        return 0;
    }
    if (strcmp(key, "ascii") == 0) {
        int x = parse_bool(value);
        if (x < 0) return -2;
        c->ascii = x;
        return 0;
    }
    if (strcmp(key, "width") == 0) {
        long x;
        if (parse_long(value, &x) != 0 || x <= 0) return -2;
        c->width = (int)x;
        return 0;
    }
    if (strcmp(key, "height") == 0) {
        long x;
        if (parse_long(value, &x) != 0 || x <= 0) return -2;
        c->height = (int)x;
        return 0;
    }
    if (strcmp(key, "trails") == 0) {
        int x = parse_bool(value);
        if (x < 0) return -2;
        c->trails = x;
        return 0;
    }
    if (strcmp(key, "collisions") == 0) {
        int x = parse_bool(value);
        if (x < 0) return -2;
        c->collisions = x;
        return 0;
    }
    if (strcmp(key, "quiet") == 0) {
        int x = parse_bool(value);
        if (x < 0) return -2;
        c->quiet = x;
        return 0;
    }
    if (strcmp(key, "seed") == 0) {
        unsigned long long x;
        if (parse_ull(value, &x) != 0) return -2;
        c->seed = x;
        return 0;
    }
    return -1;
}

/* Trim whitespace from both ends of a string. Returns the start of the trimmed string
   and updates *end to point to the end. */
static const char *trim_ends(const char *str, const char **end) {
    while (*str && isspace((unsigned char)*str)) str++;
    if (*end == NULL || *end < str) {
        *end = str + strlen(str);
    }
    while (*end > str && isspace((unsigned char)*(*end - 1))) (*end)--;
    return str;
}

int config_load_file(sim_config *c, const char *path, char *err, size_t errlen) {
    FILE *f = fopen(path, "r");
    if (!f) {
        if (err) snprintf(err, errlen, "%s: cannot open", path);
        return -1;
    }

    char line[512];
    int line_num = 0;

    while (fgets(line, sizeof line, f)) {
        line_num++;

        /* Remove CRLF or LF line endings */
        int len = (int)strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[--len] = '\0';
        }
        if (len > 0 && line[len - 1] == '\r') {
            line[--len] = '\0';
        }

        /* Trim leading whitespace */
        const char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#' || *p == ';') continue;

        /* Skip section headers like [section] */
        if (*p == '[') continue;

        /* Parse key = value */
        const char *eq = strchr(p, '=');
        if (!eq) {
            if (err) snprintf(err, errlen, "%s:%d: no '=' in line", path, line_num);
            fclose(f);
            return -1;
        }

        /* Extract and trim key */
        const char *key_end = eq;
        const char *key = trim_ends(p, &key_end);
        char key_buf[256];
        if (key_end - key >= (int)sizeof key_buf) {
            if (err) snprintf(err, errlen, "%s:%d: key too long", path, line_num);
            fclose(f);
            return -1;
        }
        memcpy(key_buf, key, (size_t)(key_end - key));
        key_buf[key_end - key] = '\0';

        /* Extract and trim value */
        const char *val = eq + 1;
        const char *val_end = val + strlen(val);
        val = trim_ends(val, &val_end);
        char val_buf[512];
        if (val_end - val >= (int)sizeof val_buf) {
            if (err) snprintf(err, errlen, "%s:%d: value too long", path, line_num);
            fclose(f);
            return -1;
        }
        memcpy(val_buf, val, (size_t)(val_end - val));
        val_buf[val_end - val] = '\0';

        /* Apply the setting */
        int res = config_set(c, key_buf, val_buf);
        if (res == -1) {
            if (err) snprintf(err, errlen, "%s:%d: unknown key '%s'", path, line_num, key_buf);
            fclose(f);
            return -1;
        }
        if (res == -2) {
            if (err) snprintf(err, errlen, "%s:%d: invalid value for '%s'", path, line_num, key_buf);
            fclose(f);
            return -1;
        }
    }

    fclose(f);
    return 0;
}
