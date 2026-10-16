/* See sataccel_image.h. Line references are to SAT-Accel src/host.cpp @ 37a0912. */
#include "sataccel_image.h"

#include <stdlib.h>
#include <string.h>

/* LMD_* bit positions (data_structures.h). Index a: 0 = positive, 1 = negative. */
#define LMD_PHASE_LO        0
#define LMD_FREE_SPACE_LO(a)  (66 + 32 * (a))
#define LMD_LATEST_PAGE_LO(a) (130 + 32 * (a))
#define LMD_NUM_ELE_LO(a)     (194 + 32 * (a))
#define LMD_ADDR_START_LO(a)  (258 + 32 * (a))

void sa_default_caps(sa_caps *caps)
{
    caps->max_vars = 8192 * 4;
    caps->max_clauses = 131072;
    caps->max_literal_elements = 256 * 4096;
    caps->max_clause_elements = 256 * 4096;
}

void sa_default_config(sa_config *cfg)
{
    cfg->lit_page_size = 16;
    cfg->reset_multiplier = 100;
    cfg->positive_phase = 0;
    cfg->decay_factor = 0.95;
}

int sa_image_init(sa_image *img, const sa_caps *caps, const sa_config *cfg)
{
    memset(img, 0, sizeof(*img));
    img->caps = *caps;
    img->cfg = *cfg;
    img->clause_store = calloc(caps->max_clause_elements, sizeof(int32_t));
    img->cmd = calloc(2 * (size_t)caps->max_clauses, sizeof(uint32_t));
    img->cls_states = calloc(2 * (size_t)caps->max_clauses, sizeof(int32_t));
    img->lit_store = calloc(caps->max_literal_elements, sizeof(int32_t));
    img->lmd = calloc((size_t)caps->max_vars, SA_LMD_BYTES);
    img->answer_stack = calloc(caps->max_vars, sizeof(int32_t));
    if (!img->clause_store || !img->cmd || !img->cls_states || !img->lit_store ||
        !img->lmd || !img->answer_stack) {
        sa_image_free(img);
        return -1;
    }
    return 0;
}

void sa_image_free(sa_image *img)
{
    free(img->clause_store);
    free(img->cmd);
    free(img->cls_states);
    free(img->lit_store);
    free(img->lmd);
    free(img->answer_stack);
    free(img->scratch);
    memset(img, 0, sizeof(*img));
}

static void set_bits(uint8_t *p, unsigned lo, unsigned width, uint64_t val)
{
    for (unsigned b = 0; b < width; b++) {
        unsigned bit = lo + b;
        if ((val >> b) & 1)
            p[bit >> 3] |= (uint8_t)(1u << (bit & 7));
        else
            p[bit >> 3] &= (uint8_t)~(1u << (bit & 7));
    }
}

uint64_t sa_lmd_field(const sa_image *img, uint32_t v, unsigned lo, unsigned hi)
{
    const uint8_t *p = img->lmd + (size_t)v * SA_LMD_BYTES;
    uint64_t val = 0;
    for (unsigned bit = lo; bit <= hi; bit++)
        val |= (uint64_t)((p[bit >> 3] >> (bit & 7)) & 1) << (bit - lo);
    return val;
}

static int cmp_int(const void *a, const void *b)
{
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

static int32_t iabs(int32_t x) { return x < 0 ? -x : x; }

/* Reserve n int32 of scratch, growing the buffer if needed. */
static int32_t *scratch(sa_image *img, size_t n)
{
    if (n > img->scratch_cap) {
        free(img->scratch);
        img->scratch = malloc(n * sizeof(int32_t));
        img->scratch_cap = img->scratch ? n : 0;
    }
    return img->scratch;
}

static void clear_buffers(sa_image *img, uint32_t num_vars)
{
    const sa_caps *c = &img->caps;
    if (img->lean_clear) {
        memset(img->clause_store, 0, img->clause_elements * sizeof(int32_t));
        memset(img->cmd, 0, 2 * (size_t)img->num_clauses * sizeof(uint32_t));
        memset(img->cls_states, 0, 2 * (size_t)img->num_clauses * sizeof(int32_t));
        memset(img->lit_store, 0, img->literal_elements * sizeof(int32_t));
    } else {                                                   /* host.cpp 164-169 */
        memset(img->clause_store, 0, c->max_clause_elements * sizeof(int32_t));
        memset(img->cmd, 0, 2 * (size_t)c->max_clauses * sizeof(uint32_t));
        memset(img->cls_states, 0, 2 * (size_t)c->max_clauses * sizeof(int32_t));
        memset(img->lit_store, 0, c->max_literal_elements * sizeof(int32_t));
    }
    memset(img->lmd, 0, (size_t)num_vars * SA_LMD_BYTES);
    memset(img->answer_stack, 0, num_vars * sizeof(int32_t));
    memset(img->misc, 0, sizeof(img->misc));                  /* host.cpp 152 */
    memset(img->track_lbd, 0, sizeof(img->track_lbd));
}

int sa_build_image(sa_image *img, uint32_t num_vars, uint32_t num_clauses,
                   const uint32_t *offsets, const int32_t *lits)
{
    const sa_caps *caps = &img->caps;
    const uint32_t P = img->cfg.lit_page_size;
    const uint32_t total = offsets[num_clauses];

    if (num_vars > caps->max_vars) return -1;                  /* host.cpp 323 */
    if (num_clauses > caps->max_clauses) return -2;            /* host.cpp 327 */

    /* Scratch layout: deduped clauses | clause offsets | occurrence counts
     * (2 per var) | occurrence starts | occurrence fill | occurrence lists
     * | decision set. */
    size_t need = (size_t)total + (num_clauses + 1) + 3 * 2 * (size_t)num_vars + 1
                + (size_t)total + (size_t)num_clauses + num_vars;
    int32_t *s = scratch(img, need);
    if (!s) return -5;
    int32_t  *cl = s;                                          /* deduped literals */
    uint32_t *co = (uint32_t *)(cl + total);                   /* their offsets    */
    uint32_t *occ_n = co + num_clauses + 1;                    /* [2*v + a]        */
    uint32_t *occ_start = occ_n + 2 * (size_t)num_vars;
    uint32_t *occ_fill = occ_start + 2 * (size_t)num_vars + 1;
    int32_t  *occ = (int32_t *)(occ_fill + 2 * (size_t)num_vars);
    int32_t  *dec = occ + total;                               /* decision set     */
    uint32_t ndec = 0;

    clear_buffers(img, num_vars);

    /* Per clause: drop repeated literals (host.cpp 113-120), then sort by
     * |literal| (124). Insertion sort is stable; host.cpp's std::sort is not,
     * so clauses containing both x and -x may order differently. */
    uint32_t n = 0;
    for (uint32_t i = 0; i < num_clauses; i++) {
        co[i] = n;
        for (uint32_t k = offsets[i]; k < offsets[i + 1]; k++) {
            int32_t l = lits[k];
            uint32_t j = co[i];
            while (j < n && cl[j] != l) j++;
            if (j == n) cl[n++] = l;
        }
        for (uint32_t a = co[i] + 1; a < n; a++) {
            int32_t key = cl[a];
            uint32_t b = a;
            while (b > co[i] && iabs(cl[b - 1]) > iabs(key)) { cl[b] = cl[b - 1]; b--; }
            cl[b] = key;
        }
    }
    co[num_clauses] = n;

    /* Occurrence lists (host.cpp 138-150): positive occurrences hold clause
     * id i+1, negative hold -(i+1); unit clauses enter the decision set. */
    memset(occ_n, 0, 2 * (size_t)num_vars * sizeof(uint32_t));
    for (uint32_t i = 0; i < num_clauses; i++) {
        for (uint32_t k = co[i]; k < co[i + 1]; k++)
            occ_n[2 * (iabs(cl[k]) - 1) + (cl[k] < 0)]++;
        if (co[i + 1] - co[i] == 1)
            dec[ndec++] = cl[co[i]];
    }
    occ_start[0] = 0;
    for (size_t x = 0; x < 2 * (size_t)num_vars; x++) {
        occ_start[x + 1] = occ_start[x] + occ_n[x];
        occ_fill[x] = occ_start[x];
    }
    for (uint32_t i = 0; i < num_clauses; i++)
        for (uint32_t k = co[i]; k < co[i + 1]; k++) {
            size_t x = 2 * (size_t)(iabs(cl[k]) - 1) + (cl[k] < 0);
            occ[occ_fill[x]++] = cl[k] > 0 ? (int32_t)(i + 1) : -(int32_t)(i + 1);
        }
    /* std::set semantics: sorted, unique. */
    qsort(dec, ndec, sizeof(int32_t), cmp_int);
    uint32_t u = 0;
    for (uint32_t k = 0; k < ndec; k++)
        if (u == 0 || dec[u - 1] != dec[k]) dec[u++] = dec[k];
    ndec = u;

    /* Clause store (host.cpp 175-231). */
    uint32_t idx = 0, presolved = 0;
    for (uint32_t i = 0; i < num_clauses; i++) {
        uint32_t len = co[i + 1] - co[i], in_page = 0;
        int32_t x = 0, earliest = 0;
        int need_zero = 1;
        img->cmd[2 * i] = idx;
        img->cmd[2 * i + 1] = len;
        for (uint32_t j = 0; j < len; j++) {
            int32_t l = cl[co[i] + j];
            img->clause_store[idx] = l;
            x ^= l;
            if (earliest == 0 && bsearch(&l, dec, ndec, sizeof(int32_t), cmp_int)) {
                earliest = l;
                presolved++;
            }
            idx++;
            in_page++;
            if (in_page == SA_CLAUSE_PAGE - 1) {
                if (j != len - 1) {
                    img->clause_store[idx] = (int32_t)(idx + 1);
                } else {
                    img->clause_store[idx] = 0;
                    need_zero = 0;
                }
                idx++;
                in_page = 0;
            }
            if (idx > caps->max_clause_elements) return -3;
        }
        if (need_zero) {
            for (uint32_t j = 0; j < SA_CLAUSE_PAGE - in_page; j++) {
                img->clause_store[idx++] = 0;
                if (idx > caps->max_clause_elements) return -3;
            }
        }
        img->cls_states[2 * i] = x;                            /* compressedList */
        img->cls_states[2 * i + 1] = (int32_t)len;             /* remainingUnassigned */
    }
    while (idx % SA_CLAUSE_PAGE != 0) img->clause_store[idx++] = 0;   /* 224-230 */
    img->clause_elements = idx;

    /* Literal metadata and occurrence pages (host.cpp 233-304). */
    idx = 0;
    for (uint32_t v = 0; v < num_vars; v++) {
        uint8_t *m = img->lmd + (size_t)v * SA_LMD_BYTES;
        uint32_t np = occ_n[2 * v], nn = occ_n[2 * v + 1];
        set_bits(m, LMD_NUM_ELE_LO(0), 32, np);
        set_bits(m, LMD_NUM_ELE_LO(1), 32, nn);
        set_bits(m, LMD_PHASE_LO, 1, !img->cfg.positive_phase);
        if (np == 0 && nn == 0) {
            for (int a = 0; a < 2; a++) {
                set_bits(m, LMD_ADDR_START_LO(a), 32, idx);
                set_bits(m, LMD_LATEST_PAGE_LO(a), 32, idx);
            }
            dec[ndec++] = (int32_t)(v + 1);                    /* 258 */
            continue;
        }
        for (int a = 0; a < 2; a++) {
            uint32_t in_page = 0;
            set_bits(m, LMD_ADDR_START_LO(a), 32, idx);
            set_bits(m, LMD_LATEST_PAGE_LO(a), 32, idx);
            for (uint32_t k = occ_start[2 * v + a]; k < occ_start[2 * v + a + 1]; k++) {
                img->lit_store[idx++] = iabs(occ[k]);
                in_page++;
                if (in_page == P - 2) {
                    img->lit_store[idx] = 0;
                    idx++;
                    img->lit_store[idx] = (int32_t)(idx + 1);
                    idx++;
                    set_bits(m, LMD_LATEST_PAGE_LO(a), 32, idx);
                    in_page = 0;
                }
                if (idx > caps->max_literal_elements) return -4;
            }
            for (uint32_t j = 0; j < P - in_page; j++) {
                img->lit_store[idx++] = 0;
                if (idx > caps->max_literal_elements) return -4;
            }
            set_bits(m, LMD_FREE_SPACE_LO(a), 32, P - in_page - 2);
        }
    }

    /* Answer stack = the decision set in std::set order (host.cpp 306-309).
     * Unused variables were appended above; re-sort the merged set. */
    qsort(dec, ndec, sizeof(int32_t), cmp_int);
    if (ndec > num_vars) return -6;    /* host.cpp would overflow answerStack */
    memcpy(img->answer_stack, dec, ndec * sizeof(int32_t));

    img->num_vars = num_vars;
    img->num_clauses = num_clauses;
    img->literal_elements = idx;
    img->decision_count = ndec;
    img->presolved_count = presolved;
    img->misc[0] = (int32_t)idx;                               /* 311-320 */
    img->misc[1] = (int32_t)num_clauses;
    img->misc[2] = (int32_t)num_vars;
    img->misc[3] = (int32_t)ndec;
    img->misc[4] = img->cfg.positive_phase ? 1 : 0;
    img->misc[5] = (int32_t)caps->max_literal_elements;
    img->misc[6] = (int32_t)P;
    img->misc[7] = (int32_t)img->cfg.reset_multiplier;
    return 0;
}

size_t sa_regions(const sa_image *img, sa_region rows[SA_NUM_REGIONS])
{
    const sa_caps *c = &img->caps;
    size_t nv = img->num_vars, nc = img->num_clauses;
    sa_region r[SA_NUM_REGIONS] = {
        {"clause_store", c->max_clause_elements * 4,  img->clause_elements * 4ul},
        {"track_lbd",    SA_TRACK_LBD_WORDS * 4,      SA_TRACK_LBD_WORDS * 4},
        {"cmd",          (size_t)c->max_clauses * 8,  nc * 8},
        {"lit_store",    c->max_literal_elements * 4, img->literal_elements * 4ul},
        {"answer_stack", nv * 4,                      nv * 4},
        {"lmd",          nv * SA_LMD_BYTES,           nv * SA_LMD_BYTES},
        {"cls_states",   (size_t)c->max_clauses * 8,  nc * 8},
        {"misc",         256 * 4,                     256 * 4},
    };
    size_t total = 0;
    for (int i = 0; i < SA_NUM_REGIONS; i++) {
        rows[i] = r[i];
        total += r[i].allocated;
    }
    return total;
}

const void *sa_region_ptr(const sa_image *img, int region)
{
    switch (region) {
    case 0: return img->clause_store;
    case 1: return img->track_lbd;
    case 2: return img->cmd;
    case 3: return img->lit_store;
    case 4: return img->answer_stack;
    case 5: return img->lmd;
    case 6: return img->cls_states;
    case 7: return img->misc;
    default: return NULL;
    }
}

long sa_decode_clauses(const sa_image *img, uint32_t **out_offsets, int32_t **out_lits)
{
    uint32_t nc = img->num_clauses;
    size_t total = 0;
    for (uint32_t i = 0; i < nc; i++) total += img->cmd[2 * i + 1];
    uint32_t *off = malloc((nc + 1) * sizeof(uint32_t));
    int32_t *lits = malloc((total ? total : 1) * sizeof(int32_t));
    if (!off || !lits) { free(off); free(lits); return -1; }

    size_t n = 0;
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t pos = img->cmd[2 * i], len = img->cmd[2 * i + 1], in_page = 0;
        off[i] = (uint32_t)n;
        for (uint32_t j = 0; j < len; j++) {
            if (pos >= img->clause_elements) goto bad;
            lits[n++] = img->clause_store[pos++];
            if (++in_page == SA_CLAUSE_PAGE - 1 && j != len - 1) {
                pos = (uint32_t)img->clause_store[pos];        /* next-page link */
                in_page = 0;
            }
        }
    }
    off[nc] = (uint32_t)n;
    *out_offsets = off;
    *out_lits = lits;
    return (long)nc;
bad:
    free(off);
    free(lits);
    return -1;
}
