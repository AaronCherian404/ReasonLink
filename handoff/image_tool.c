/*
 * Build a SAT-Accel memory image from a DIMACS file with the C port.
 *
 *   image_tool <file.cnf> [outdir]
 *
 * Prints the per-region byte table, checks that the device-side decode of
 * the image returns the (de-duplicated, |lit|-sorted) input clauses, and, if
 * outdir is given, dumps each region as <outdir>/<region>.bin for the golden
 * comparison (same files golden_parse writes; track_lbd is not dumped since
 * host.cpp never initialises it).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cnf.h"
#include "sataccel_image.h"

static int dump(const char *dir, const char *name, const void *p, size_t n)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/%s.bin", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(p, 1, n, f) != n) {
        perror(path);
        return -1;
    }
    fclose(f);
    return 0;
}

/* Decode must return each input clause with duplicates removed, in |lit| order. */
static int check_decode(const sa_image *img, const rl_cnf *cnf)
{
    uint32_t *off;
    int32_t *lits;
    long nc = sa_decode_clauses(img, &off, &lits);
    if (nc != (long)cnf->num_clauses) return -1;
    int bad = 0;
    for (uint32_t i = 0; i < cnf->num_clauses && !bad; i++) {
        /* every input literal appears in the decoded clause and vice versa */
        for (uint32_t k = cnf->offsets[i]; k < cnf->offsets[i + 1] && !bad; k++) {
            int found = 0;
            for (uint32_t j = off[i]; j < off[i + 1]; j++) found |= lits[j] == cnf->lits[k];
            bad = !found;
        }
        for (uint32_t j = off[i] + 1; j < off[i + 1] && !bad; j++)
            bad = abs(lits[j - 1]) > abs(lits[j]);
    }
    free(off);
    free(lits);
    return bad ? -1 : 0;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s file.cnf [outdir]\n", argv[0]);
        return 2;
    }
    rl_cnf cnf;
    if (rl_cnf_read_dimacs(&cnf, argv[1])) {
        fprintf(stderr, "cannot read %s\n", argv[1]);
        return 1;
    }
    sa_caps caps;
    sa_config cfg;
    sa_default_caps(&caps);
    sa_default_config(&cfg);
    sa_image img;
    if (sa_image_init(&img, &caps, &cfg)) return 1;
    int rc = sa_build_image(&img, cnf.num_vars, cnf.num_clauses, cnf.offsets, cnf.lits);
    if (rc) {
        fprintf(stderr, "sa_build_image failed (%d): capacity exceeded\n", rc);
        return 1;
    }

    sa_region rows[SA_NUM_REGIONS];
    size_t total = sa_regions(&img, rows), used = 0;
    printf("ours:   vars=%u clauses=%u clause_elements=%u literal_elements=%u decisions=%u\n",
           img.num_vars, img.num_clauses, img.clause_elements, img.literal_elements, img.decision_count);
    printf("%-13s %12s %12s\n", "region", "allocated_B", "used_B");
    for (int i = 0; i < SA_NUM_REGIONS; i++) {
        printf("%-13s %12zu %12zu\n", rows[i].name, rows[i].allocated, rows[i].used);
        used += rows[i].used;
    }
    printf("%-13s %12zu %12zu\n", "total", total, used);
    printf("decode check: %s\n", check_decode(&img, &cnf) ? "FAIL" : "ok");

    if (argc > 2) {
        for (int i = 0; i < SA_NUM_REGIONS; i++) {
            if (!strcmp(rows[i].name, "track_lbd")) continue;
            if (dump(argv[2], rows[i].name, sa_region_ptr(&img, i), rows[i].allocated)) return 1;
        }
    }
    sa_image_free(&img);
    rl_cnf_free(&cnf);
    return 0;
}
