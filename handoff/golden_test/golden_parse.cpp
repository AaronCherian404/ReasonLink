// Golden reference: SAT-Accel's own parseDIMACS (host.cpp @ 37a0912), compiled
// unmodified on the host and used to dump the memory image it builds.
//
// The function body is cut from third_party/FPGA25_SAT_Accel/src/host.cpp by
// the Makefile (parse_dimacs.inc) so it cannot drift from the pinned source.
//
// Usage: golden_parse <file.cnf> <configuration.json> <outdir>
// Writes <outdir>/<region>.bin in the canonical layout of sataccel_image.h.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/istreamwrapper.h"
#include "rapidjson/rapidjson.h"

#include "data_structures.h"
#include "fpga_solver.h"

bool compareFuncInput(int a, int b) { return abs(a) < abs(b); }   // host.cpp 41-43

#include "parse_dimacs.inc"

static void dump(const std::string &dir, const char *name, const void *p, size_t n)
{
    std::string path = dir + "/" + name + ".bin";
    FILE *f = fopen(path.c_str(), "wb");
    if (!f || fwrite(p, 1, n, f) != n) {
        std::perror(path.c_str());
        std::exit(EXIT_FAILURE);
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        std::fprintf(stderr, "usage: %s file.cnf configuration.json outdir\n", argv[0]);
        return 2;
    }
    rapidjson::Document cfg;
    std::ifstream cf(argv[2]);
    rapidjson::IStreamWrapper isw(cf);
    cfg.ParseStream(isw);

    problemData pd;
    parseDIMACS(argv[1], pd, cfg);
    const std::string out = argv[3];
    const unsigned nv = pd.md.numLiterals;

    static_assert(sizeof(clauseMetaData) == 8 && sizeof(clsStatePCIE) == 8, "layout");
    dump(out, "clause_store", pd.clauseStore, _HOST_MAX_CLAUSE_ELEMENTS * sizeof(cls));
    dump(out, "cmd", pd.cmd, _FPGA_MAX_CLAUSES * sizeof(clauseMetaData));
    dump(out, "lit_store", pd.litStore, _HOST_MAX_LITERAL_ELEMENTS * sizeof(lit));
    dump(out, "answer_stack", pd.answerStack, nv * sizeof(lit));
    dump(out, "cls_states", pd.clsStates, _FPGA_MAX_CLAUSES * sizeof(clsStatePCIE));
    dump(out, "misc", pd.md.miscCounters, 256 * sizeof(int));

    // ap_uint<512> -> 64 bytes, bit i in byte i/8 (what the kernel's
    // literalMetaDataPCIE port sees). Extracted via range() so the result does
    // not depend on the host ap_int implementation's object layout.
    std::vector<unsigned char> lmd(nv * 64u);
    for (unsigned v = 0; v < nv; v++)
        for (unsigned b = 0; b < 64; b++)
            lmd[v * 64 + b] = (unsigned char)pd.lmd[v].compactlmd.range(8 * b + 7, 8 * b).to_uint();
    dump(out, "lmd", lmd.data(), lmd.size());

    std::printf("golden: vars=%u clauses=%u clause_elements=%u literal_elements=%u decisions=%d\n",
                nv, pd.md.numClauses, pd.md.clauseElements, pd.md.literalElements, pd.md.miscCounters[3]);
    return 0;
}
