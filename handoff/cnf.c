#include "cnf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int reserve(rl_cnf *c, uint32_t clauses, uint32_t lits)
{
    if (clauses + 1 > c->cap_clauses) {
        uint32_t cap = clauses + 1;
        uint32_t *p = realloc(c->offsets, cap * sizeof(uint32_t));
        if (!p) return -1;
        c->offsets = p;
        c->cap_clauses = cap;
    }
    if (lits > c->cap_lits) {
        uint32_t cap = lits > 2 * c->cap_lits ? lits : 2 * c->cap_lits;
        int32_t *p = realloc(c->lits, cap * sizeof(int32_t));
        if (!p) return -1;
        c->lits = p;
        c->cap_lits = cap;
    }
    return 0;
}

int rl_cnf_read_dimacs(rl_cnf *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    memset(c, 0, sizeof(*c));
    char line[1 << 16];
    uint32_t declared = 0, n = 0, nc = 0;
    int have_header = 0;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == 'c' || *p == '\n' || *p == '\0') continue;
        if (*p == '%') break;
        if (*p == 'p') {
            if (sscanf(p, "p cnf %u %u", &c->num_vars, &declared) != 2) goto fail;
            if (reserve(c, declared, declared * 3)) goto fail;
            c->offsets[0] = 0;
            have_header = 1;
            continue;
        }
        if (!have_header) goto fail;
        for (;;) {
            char *end;
            long v = strtol(p, &end, 10);
            if (end == p) break;
            p = end;
            if (v == 0) {
                if (reserve(c, nc + 1, n)) goto fail;
                c->offsets[++nc] = n;
            } else {
                if (reserve(c, nc, n + 1)) goto fail;
                c->lits[n++] = (int32_t)v;
            }
        }
    }
    fclose(f);
    c->num_clauses = nc;
    return have_header ? 0 : -1;
fail:
    fclose(f);
    rl_cnf_free(c);
    return -1;
}

void rl_cnf_free(rl_cnf *c)
{
    free(c->offsets);
    free(c->lits);
    memset(c, 0, sizeof(*c));
}

int rl_cnf_with_units(rl_cnf *d, const rl_cnf *b, const int32_t *units, uint32_t k)
{
    uint32_t nl = b->offsets[b->num_clauses];
    if (reserve(d, b->num_clauses + k, nl + k)) return -1;
    memcpy(d->offsets, b->offsets, (b->num_clauses + 1) * sizeof(uint32_t));
    memcpy(d->lits, b->lits, nl * sizeof(int32_t));
    for (uint32_t i = 0; i < k; i++) {
        d->lits[nl + i] = units[i];
        d->offsets[b->num_clauses + i + 1] = nl + i + 1;
    }
    d->num_vars = b->num_vars;
    d->num_clauses = b->num_clauses + k;
    return 0;
}
