#include "trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = n > 0 ? malloc((size_t)n) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *len = buf ? (size_t)n : 0;
    return buf;
}

static uint32_t u32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }

int rl_trace_read(rl_trace *t, const char *path)
{
    size_t len;
    uint8_t *buf = read_file(path, &len);
    memset(t, 0, sizeof(*t));
    if (!buf || len < 32 || memcmp(buf, "RLTR", 4) || u32(buf + 4) != 1) {
        free(buf);
        return -1;
    }
    t->num_puzzles = u32(buf + 8);
    t->cells = u32(buf + 12);
    t->classes = u32(buf + 16);
    t->stride = u32(buf + 20);
    memcpy(&t->ip2_scale, buf + 24, 4);
    size_t rec = t->cells + (size_t)t->cells * t->stride;
    if (t->cells != RL_CELLS || len != 32 + rec * t->num_puzzles) {
        free(buf);
        return -1;
    }
    t->data = malloc(len - 32);
    memcpy(t->data, buf + 32, len - 32);
    free(buf);
    return 0;
}

void rl_trace_free(rl_trace *t)
{
    free(t->data);
    memset(t, 0, sizeof(*t));
}

const uint8_t *rl_trace_mask(const rl_trace *t, uint32_t p)
{
    return t->data + (size_t)p * (t->cells + (size_t)t->cells * t->stride);
}

const int8_t *rl_trace_scores(const rl_trace *t, uint32_t p)
{
    return (const int8_t *)(rl_trace_mask(t, p) + t->cells);
}

uint32_t rl_argmax_units(const rl_trace *t, uint32_t p, int32_t *units)
{
    const uint8_t *mask = rl_trace_mask(t, p);
    const int8_t *s = rl_trace_scores(t, p);
    uint32_t n = 0;
    for (uint32_t c = 0; c < t->cells; c++) {
        if (!mask[c]) continue;
        const int8_t *cell = s + (size_t)c * t->stride;
        int best = 1;
        for (int d = 2; d <= RL_DIGITS; d++)
            if (cell[d] > cell[best]) best = d;
        units[n++] = (int32_t)(9 * c + best);
    }
    return n;
}

#define REF_REC (1 + 2 * RL_CELLS)

int rl_reference_read(rl_reference *r, const char *path)
{
    size_t len;
    uint8_t *buf = read_file(path, &len);
    memset(r, 0, sizeof(*r));
    if (!buf || len < 12 || memcmp(buf, "RLRF", 4) || u32(buf + 4) != 1) {
        free(buf);
        return -1;
    }
    r->num_puzzles = u32(buf + 8);
    if (len != 12 + (size_t)REF_REC * r->num_puzzles) {
        free(buf);
        return -1;
    }
    r->data = malloc(len - 12);
    memcpy(r->data, buf + 12, len - 12);
    free(buf);
    return 0;
}

void rl_reference_free(rl_reference *r)
{
    free(r->data);
    memset(r, 0, sizeof(*r));
}

int rl_reference_sat(const rl_reference *r, uint32_t p) { return r->data[(size_t)p * REF_REC]; }

const int8_t *rl_reference_solved(const rl_reference *r, uint32_t p)
{
    return (const int8_t *)r->data + (size_t)p * REF_REC + 1;
}

const int8_t *rl_reference_givens(const rl_reference *r, uint32_t p)
{
    return (const int8_t *)r->data + (size_t)p * REF_REC + 1 + RL_CELLS;
}
