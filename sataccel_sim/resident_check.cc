// ReasonLink Milestone 2 check: the base formula resident in SAT-Accel.
//
// With sataccel_hw/patches/0002-resident-base.patch, residentBase = 1 makes
// the solver skip copying in the literal store (restoring each occurrence
// list's base end from the copied-in metadata and clearing the tails learned
// clauses wrote) and the clause store handler skip copying in the clause
// store and clause metadata. Instances that share a base formula and differ
// only in their fixed decisions (the givens, as in Milestone 1's delta) can
// then run back to back without reloading the base.
//
// For each workload this runs every instance twice through SAT-Accel's
// functional simulation:
//   fresh     each instance with the full copy-in (residentBase = 0)
//   resident  the first instance fresh, every later one with residentBase = 1
// and requires identical results: SAT/UNSAT, the model, and SAT-Accel's
// search counters. It also checks the on-chip state directly: after every
// resident copy-in, the base regions of the literal store, the clause store
// and the clause metadata must equal what a fresh copy-in produces, byte for
// byte; and it counts how often the literal store differed before its tails
// were cleared (i.e. how often the clearing was needed).
//
//   resident_check sudoku <data/sudoku> [N]
//   resident_check cnf <K> <seed> <file.cnf> ...   K random fixed-decision sets per formula
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

#include "cnf.h"
#include "sataccel_backend.h"
#include "trace.h"

// ---- probes (called by the patched kernels; see patch 0002) ----
namespace {
struct Region {
    const char *name;
    std::vector<unsigned char> ref;   // after the last fresh copy-in
    unsigned long checked = 0, mismatched = 0;
};
Region g_region[3] = {{"literal store"}, {"clause store"}, {"clause metadata"}};
unsigned long g_dirty_runs = 0, g_dirty_bytes = 0, g_resident_runs_probed = 0;

void probe(int k, const void *p, unsigned long n, unsigned resident)
{
    const unsigned char *b = static_cast<const unsigned char *>(p);
    Region &r = g_region[k];
    if (!resident) {
        r.ref.assign(b, b + n);
        return;
    }
    r.checked++;
    if (n != r.ref.size() || std::memcmp(b, r.ref.data(), n)) r.mismatched++;
}
}  // namespace

extern "C" void rl_probe_solver(const void *p, unsigned long n, unsigned flags)
{
    if (flags & 2) {   // resident, before the tails are cleared
        const unsigned char *b = static_cast<const unsigned char *>(p);
        const std::vector<unsigned char> &ref = g_region[0].ref;
        unsigned long d = 0;
        for (unsigned long i = 0; i < n && i < ref.size(); i++) d += b[i] != ref[i];
        g_resident_runs_probed++;
        if (d) {
            g_dirty_runs++;
            g_dirty_bytes += d;
        }
        return;
    }
    probe(0, p, n, flags & 1);
}

extern "C" void rl_probe_csh(const void *p, unsigned long n, unsigned flags)
{
    probe((flags & 2) ? 2 : 1, p, n, flags & 1);
}

// ---- instances ----
namespace {
struct Outcome {
    int status;
    uint32_t iterations, decisions, retries, backtracks, restarts;
    std::vector<int8_t> model;
    bool operator==(const Outcome &o) const
    {
        return status == o.status && iterations == o.iterations && decisions == o.decisions &&
               retries == o.retries && backtracks == o.backtracks && restarts == o.restarts &&
               (status != 1 || model == o.model);
    }
};

// One base image; instances = fixed-decision sets (sorted, std::set order).
struct Workload {
    std::string name;
    sa_image img;
    std::vector<int32_t> base_answer;
    int32_t base_misc[256];
    std::vector<std::vector<int32_t>> fixed;
    const rl_cnf *cnf = nullptr;   // for the model check (cnf workloads)
};

void set_instance(Workload &w, size_t k)
{
    const uint32_t nv = w.img.num_vars;
    std::memcpy(w.img.answer_stack, w.base_answer.data(), nv * sizeof(int32_t));
    std::memcpy(w.img.misc, w.base_misc, sizeof w.base_misc);
    std::memcpy(w.img.answer_stack, w.fixed[k].data(), w.fixed[k].size() * sizeof(int32_t));
    w.img.misc[3] = (int32_t)w.fixed[k].size();
}

bool solve(Workload &w, size_t k, unsigned resident, Outcome *o)
{
    set_instance(w, k);
    o->model.assign(w.img.num_vars, 0);
    rl_sataccel_result r;
    int st = rl_sataccel_solve_image_ex(&w.img, o->model.data(), 600.0, resident, &r);
    if (st == -2) {
        std::fprintf(stderr, "%s instance %zu: timeout\n", w.name.c_str(), k);
        std::exit(3);   // kernel threads cannot be reclaimed
    }
    o->status = st;
    o->iterations = r.iterations;
    o->decisions = r.decisions;
    o->retries = r.retries;
    o->backtracks = r.backtracks;
    o->restarts = r.restarts;
    if (st != 1) o->model.clear();
    return st >= 0;
}

// SAT models must satisfy the formula and the instance's fixed decisions
bool model_ok(const Workload &w, size_t k, const Outcome &o)
{
    if (o.status != 1 || !w.cnf) return true;
    for (int32_t l : w.fixed[k]) {
        uint32_t v = (uint32_t)std::abs(l) - 1;
        if (o.model[v] != (l > 0 ? 1 : -1)) return false;
    }
    const rl_cnf *c = w.cnf;
    for (uint32_t i = 0; i < c->num_clauses; i++) {
        bool sat = false;
        for (uint32_t j = c->offsets[i]; j < c->offsets[i + 1] && !sat; j++) {
            int32_t l = c->lits[j];
            sat = o.model[std::abs(l) - 1] == (l > 0 ? 1 : -1);
        }
        if (!sat) return false;
    }
    return true;
}

struct Totals {
    unsigned workloads = 0, instances = 0, errors = 0, same = 0, models_bad = 0, sat = 0, unsat = 0;
    unsigned long conflicts = 0;
} g_tot;

void run(Workload &w)
{
    const size_t n = w.fixed.size();
    std::vector<Outcome> fresh(n), res(n);
    unsigned errs = 0, same = 0, bad = 0;
    for (size_t k = 0; k < n; k++)
        if (!solve(w, k, 0, &fresh[k])) errs++;
    for (size_t k = 0; k < n; k++)
        if (!solve(w, k, k == 0 ? 0 : 1, &res[k])) errs++;
    unsigned long confl = 0;
    unsigned sat = 0;
    for (size_t k = 0; k < n; k++) {
        if (fresh[k] == res[k]) same++;
        else std::printf("  %s instance %zu: resident run differs (status %d/%d, decisions %u/%u, conflicts %u/%u)\n",
                         w.name.c_str(), k, fresh[k].status, res[k].status, fresh[k].decisions, res[k].decisions,
                         fresh[k].backtracks, res[k].backtracks);
        if (!model_ok(w, k, fresh[k]) || !model_ok(w, k, res[k])) bad++;
        confl += fresh[k].backtracks;
        sat += fresh[k].status == 1;
    }
    std::printf("%-28s instances %4zu  SAT %4u UNSAT %4zu  conflicts %7lu  resident == fresh %4u/%zu  errors %u%s\n",
                w.name.c_str(), n, sat, n - sat, confl, same, n, errs, bad ? "  MODEL CHECK FAILED" : "");
    std::fflush(stdout);
    g_tot.workloads++;
    g_tot.instances += n;
    g_tot.errors += errs;
    g_tot.same += same;
    g_tot.models_bad += bad;
    g_tot.sat += sat;
    g_tot.unsat += n - sat;
    g_tot.conflicts += confl;
}

bool make_workload(Workload &w, const rl_cnf &c, const char *name)
{
    sa_caps caps;
    sa_config cfg;
    rl_sataccel_caps(&caps);
    sa_default_config(&cfg);
    w.name = name;
    if (c.num_vars > caps.max_vars / 2) {   // pqHandler heap bound (PROGRESS P2-2)
        std::printf("%-28s skipped: %u variables > max_vars/2\n", name, c.num_vars);
        return false;
    }
    if (sa_image_init(&w.img, &caps, &cfg) || sa_build_image(&w.img, c.num_vars, c.num_clauses, c.offsets, c.lits)) {
        std::printf("%-28s skipped: does not fit the image capacities\n", name);
        return false;
    }
    w.base_answer.assign(w.img.answer_stack, w.img.answer_stack + w.img.num_vars);
    std::memcpy(w.base_misc, w.img.misc, sizeof w.base_misc);
    return true;
}

void print_probes()
{
    std::printf("on-chip base after each resident copy-in, vs a fresh copy-in:\n");
    for (const Region &r : g_region)
        std::printf("  %-16s %lu checked, %lu differ\n", r.name, r.checked, r.mismatched);
    std::printf("  literal store before its tails were cleared: differed in %lu of %lu resident runs (%lu bytes)\n",
                g_dirty_runs, g_resident_runs_probed, g_dirty_bytes);
}

int usage(const char *a0)
{
    std::fprintf(stderr, "usage: %s sudoku <data/sudoku> [N]\n       %s cnf <K> <seed> <file.cnf>...\n", a0, a0);
    return 2;
}
}  // namespace

int main(int argc, char **argv)
{
    if (argc < 3) return usage(argv[0]);
    std::string mode = argv[1];
    if (mode == "sudoku") {
        char p[4096];
        rl_cnf base;
        rl_trace tr;
        std::snprintf(p, sizeof p, "%s/base.cnf", argv[2]);
        if (rl_cnf_read_dimacs(&base, p)) return 2;
        std::snprintf(p, sizeof p, "%s/trace.bin", argv[2]);
        if (rl_trace_read(&tr, p)) return 2;
        uint32_t n = argc > 3 ? (uint32_t)std::atoi(argv[3]) : tr.num_puzzles;
        Workload w;
        if (!make_workload(w, base, "sudoku (predicted givens)")) return 2;
        if (w.img.decision_count != 0) {
            std::fprintf(stderr, "base formula has %u decisions; expected 0\n", w.img.decision_count);
            return 2;
        }
        for (uint32_t k = 0; k < n; k++) {
            int32_t units[RL_CELLS];
            uint32_t u = rl_argmax_units(&tr, k, units);
            std::vector<int32_t> d(units, units + u);
            std::sort(d.begin(), d.end());
            d.erase(std::unique(d.begin(), d.end()), d.end());
            w.fixed.push_back(d);
        }
        w.cnf = &base;
        run(w);
    } else if (mode == "cnf") {
        if (argc < 5) return usage(argv[0]);
        int K = std::atoi(argv[2]);
        std::mt19937 rng((uint32_t)std::atoi(argv[3]));
        for (int a = 4; a < argc; a++) {
            rl_cnf *c = new rl_cnf;
            if (rl_cnf_read_dimacs(c, argv[a])) {
                std::printf("%s: cannot read\n", argv[a]);
                continue;
            }
            const char *slash = std::strrchr(argv[a], '/');
            Workload w;
            if (!make_workload(w, *c, slash ? slash + 1 : argv[a])) continue;
            w.cnf = c;
            // Base decisions (unit clauses, unused variables) plus random
            // fixed literals on other variables: instance 0 none, then
            // 1..max(1, num_vars/8) of them.
            std::vector<int32_t> d0(w.img.answer_stack, w.img.answer_stack + w.img.decision_count);
            std::set<uint32_t> taken;
            for (int32_t l : d0) taken.insert((uint32_t)std::abs(l));
            std::vector<uint32_t> free_vars;
            for (uint32_t v = 1; v <= c->num_vars; v++)
                if (!taken.count(v)) free_vars.push_back(v);
            uint32_t maxk = std::max<uint32_t>(1, (uint32_t)free_vars.size() / 8);
            for (int k = 0; k < K; k++) {
                std::set<int32_t> s(d0.begin(), d0.end());
                if (k > 0 && !free_vars.empty()) {
                    std::shuffle(free_vars.begin(), free_vars.end(), rng);
                    uint32_t m = 1 + rng() % std::min<uint32_t>(maxk, (uint32_t)free_vars.size());
                    for (uint32_t i = 0; i < m; i++)
                        s.insert(rng() & 1 ? (int32_t)free_vars[i] : -(int32_t)free_vars[i]);
                }
                w.fixed.emplace_back(s.begin(), s.end());
            }
            run(w);
        }
    } else {
        return usage(argv[0]);
    }
    print_probes();
    bool probes_ok = true;
    for (const Region &r : g_region) probes_ok = probes_ok && r.mismatched == 0;
    bool ok = g_tot.errors == 0 && g_tot.same == g_tot.instances && g_tot.models_bad == 0 && probes_ok;
    std::printf("%s: %u workloads, %u instances (%u SAT, %u UNSAT, %lu conflicts), resident == fresh %u/%u, "
                "errors %u, model check failures %u\n",
                ok ? "PASS" : "FAIL", g_tot.workloads, g_tot.instances, g_tot.sat, g_tot.unsat, g_tot.conflicts,
                g_tot.same, g_tot.instances, g_tot.errors, g_tot.models_bad);
    return ok ? 0 : 1;
}
