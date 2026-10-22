// See sataccel_backend.h. Kernel wiring follows SAT-Accel src/k2k.cfg;
// argument values follow host.cpp's setArg calls (lines 520-541).
#include "sataccel_backend.h"

#include <pthread.h>
#include <time.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

#include "data_structures.h"
#include "fpga_solver.h"

extern "C" {
void solver(clsStatePCIE *clsStates, ap_int<512> *litStore, lit *answerStack,
            literalMetaDataPCIE *lmd, int *miscCounters,
            hls::stream<ap_axiu<32, 0, 0, 0>> &pqHandlerValue, hls::stream<ap_axiu<32, 0, 0, 0>> &pqHandlerInput,
            hls::stream<ap_axiu<96, 0, 0, 0>> &clauseStoreInputStream1,
            hls::stream<ap_axiu<96, 0, 0, 0>> &clauseStoreInputStream2,
            hls::stream<ap_axiu<32, 0, 0, 0>> &clauseStoreOutputStream1,
            hls::stream<ap_axiu<32, 0, 0, 0>> &clauseStoreOutputStream2,
            hls::stream<ap_axiu<32, 0, 0, 0>> &locationOutputStream,
            hls::stream<ap_axiu<32, 0, 0, 0>> &restartValueStream, hls::stream<ap_axiu<1, 0, 0, 0>> &stopStream,
            hls::stream<ap_axiu<64, 0, 0, 0>> &timerValueStream, hls::stream<ap_axiu<1, 0, 0, 0>> &conditionStream,
            hls::stream<ap_axiu<96, 0, 0, 0>> &messageStream);
void clause_store_handler(ap_uint<128> *clauseStore, clauseMetaData *cmd, cls *usedClsIDBuckets,
                          unsigned int *trackLBD, const unsigned int initialClauseElements,
                          const unsigned int maxClauseElements, const unsigned int initialClauseCount,
                          const unsigned int clausePageSize, const double prunePctage,
                          hls::stream<ap_axiu<96, 0, 0, 0>> &clauseStoreInputStream1,
                          hls::stream<ap_axiu<96, 0, 0, 0>> &clauseStoreInputStream2,
                          hls::stream<ap_axiu<32, 0, 0, 0>> &clauseStoreOutputStream1,
                          hls::stream<ap_axiu<32, 0, 0, 0>> &clauseStoreOutputStream2,
                          hls::stream<ap_axiu<64, 0, 0, 0>> &locationInputStream);
void location_handler(hls::stream<ap_axiu<64, 0, 0, 0>> &locationInputStream,
                      hls::stream<ap_axiu<32, 0, 0, 0>> &locationOutputStream);
void restartCalculator(hls::stream<ap_axiu<32, 0, 0, 0>> &value, hls::stream<ap_axiu<1, 0, 0, 0>> &stop);
void timer(hls::stream<ap_axiu<1, 0, 0, 0>> &condition, hls::stream<ap_axiu<64, 0, 0, 0>> &value);
void pqHandler(int num_literals, double decay, hls::stream<ap_axiu<32, 0, 0, 0>> &input,
               hls::stream<ap_axiu<32, 0, 0, 0>> &output);
void message(int *debug, bool enableDebug, hls::stream<ap_axiu<96, 0, 0, 0>> &messageStream);
}

namespace {

constexpr size_t kStack = 512ull << 20;   // kernels keep MBs of "on-chip" arrays on the stack
constexpr double kPrunePercentage = 0.1;  // configuration.json
constexpr unsigned kClausePageSize = 4;

// Device-side buffers, typed as the kernels expect (allocated once).
struct DeviceBuffers {
    std::vector<ap_uint<128>> clauseStore;
    std::vector<clauseMetaData> cmd;
    std::vector<cls> usedClsIDBuckets;
    std::vector<unsigned int> trackLBD;
    std::vector<clsStatePCIE> clsStates;
    std::vector<ap_int<512>> litStore;
    std::vector<lit> answerStack;
    std::vector<literalMetaDataPCIE> lmd;
    std::vector<int> misc, debug;
};

DeviceBuffers &buffers()
{
    static DeviceBuffers b;
    if (b.clauseStore.empty()) {
        b.clauseStore.resize(_HOST_MAX_CLAUSE_ELEMENTS / 4);
        b.cmd.resize(_FPGA_MAX_CLAUSES);
        b.usedClsIDBuckets.resize((size_t)_FPGA_MAX_LBD_BUCKETS * _FPGA_MAX_CLAUSES);
        b.trackLBD.resize(2 * _FPGA_MAX_LBD_BUCKETS);
        b.clsStates.resize(_FPGA_MAX_CLAUSES);
        b.litStore.resize(_HOST_MAX_LITERAL_ELEMENTS / 16);
        b.misc.resize(256);
        b.debug.resize(8192);
    }
    return b;
}

// "Migration": the image's raw little-endian words -> the kernels' types.
// Done field by field so it does not depend on ap_int's object layout.
void load(DeviceBuffers &d, const sa_image *img)
{
    const uint32_t nv = img->num_vars;
    for (size_t i = 0; i < d.clauseStore.size(); i++) {
        ap_uint<128> w = 0;
        for (int j = 0; j < 4; j++) w.range(32 * j + 31, 32 * j) = (uint32_t)img->clause_store[4 * i + j];
        d.clauseStore[i] = w;
    }
    for (size_t i = 0; i < d.cmd.size(); i++) {
        d.cmd[i].addressStart = img->cmd[2 * i];
        d.cmd[i].numElements = img->cmd[2 * i + 1];
        d.clsStates[i].compressedList = img->cls_states[2 * i];
        d.clsStates[i].remainingUnassigned = img->cls_states[2 * i + 1];
    }
    for (size_t i = 0; i < d.litStore.size(); i++) {
        ap_int<512> w = 0;
        for (int j = 0; j < 16; j++) w.range(32 * j + 31, 32 * j) = (uint32_t)img->lit_store[16 * i + j];
        d.litStore[i] = w;
    }
    d.answerStack.assign(img->answer_stack, img->answer_stack + nv);
    d.lmd.assign(nv, literalMetaDataPCIE());
    for (uint32_t v = 0; v < nv; v++) {
        ap_uint<512> w = 0;
        const uint8_t *p = img->lmd + (size_t)v * SA_LMD_BYTES;
        for (int b = 0; b < SA_LMD_BYTES; b++) w.range(8 * b + 7, 8 * b) = p[b];
        d.lmd[v].compactlmd = w;
    }
    std::memcpy(d.misc.data(), img->misc, sizeof(img->misc));
    std::fill(d.trackLBD.begin(), d.trackLBD.end(), 0u);  // host.cpp leaves it uninitialised
    std::fill(d.usedClsIDBuckets.begin(), d.usedClsIDBuckets.end(), 0);  // device-only buffer
    std::fill(d.debug.begin(), d.debug.end(), 0);
}

struct Thread {
    pthread_t tid{};
    std::function<void()> fn;
    std::atomic<bool> done{false};
};

void *trampoline(void *arg)
{
    Thread *t = static_cast<Thread *>(arg);
    t->fn();
    t->done = true;
    return nullptr;
}

}  // namespace

extern "C" int rl_sataccel_solve_image(const sa_image *img, int8_t *model, double timeout_s,
                                       rl_sataccel_result *out)
{
    std::memset(out, 0, sizeof(*out));
    DeviceBuffers &d = buffers();
    load(d, img);

    // Streams, named after k2k.cfg's stream_connect lines.
    hls::stream<ap_axiu<32, 0, 0, 0>> csOut1("clauseStoreOutputStream1"), csOut2("clauseStoreOutputStream2");
    hls::stream<ap_axiu<96, 0, 0, 0>> csIn1("clauseStoreInputStream1"), csIn2("clauseStoreInputStream2");
    hls::stream<ap_axiu<64, 0, 0, 0>> locIn("locationInputStream");
    hls::stream<ap_axiu<32, 0, 0, 0>> locOut("locationOutputStream");
    hls::stream<ap_axiu<32, 0, 0, 0>> restartValue("restartValueStream");
    hls::stream<ap_axiu<1, 0, 0, 0>> stop("stopStream");
    hls::stream<ap_axiu<64, 0, 0, 0>> timerValue("timerValueStream");
    hls::stream<ap_axiu<1, 0, 0, 0>> condition("conditionStream");
    hls::stream<ap_axiu<32, 0, 0, 0>> pqValue("pqHandlerValue"), pqInput("pqHandlerInput");
    hls::stream<ap_axiu<96, 0, 0, 0>> msg("messageStream");

    const unsigned clauseElements = img->clause_elements, numClauses = img->num_clauses;
    const int numVars = (int)img->num_vars;
    const double decay = img->cfg.decay_factor;
    std::vector<std::unique_ptr<Thread>> th;
    auto spawn = [&](std::function<void()> fn) {
        th.emplace_back(new Thread);
        th.back()->fn = std::move(fn);
    };
    spawn([&] { message(d.debug.data(), false, msg); });
    spawn([&] {
        clause_store_handler(d.clauseStore.data(), d.cmd.data(), d.usedClsIDBuckets.data(), d.trackLBD.data(),
                             clauseElements, _HOST_MAX_CLAUSE_ELEMENTS, numClauses, kClausePageSize,
                             kPrunePercentage, csIn1, csIn2, csOut1, csOut2, locIn);
    });
    spawn([&] { location_handler(locIn, locOut); });
    spawn([&] { timer(condition, timerValue); });
    spawn([&] { restartCalculator(restartValue, stop); });
    spawn([&] { pqHandler(numVars, decay, pqInput, pqValue); });
    spawn([&] {
        solver(d.clsStates.data(), d.litStore.data(), d.answerStack.data(), d.lmd.data(), d.misc.data(),
               pqValue, pqInput, csIn1, csIn2, csOut1, csOut2, locOut, restartValue, stop, timerValue,
               condition, msg);
    });

    auto t0 = std::chrono::steady_clock::now();
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, kStack);
    for (auto &t : th)
        if (pthread_create(&t->tid, &attr, trampoline, t.get())) {
            pthread_attr_destroy(&attr);
            out->status = -1;
            return -1;
        }
    pthread_attr_destroy(&attr);

    // Wait for every kernel to return (each exits on its EXIT message).
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)timeout_s;
    for (auto &t : th) {
        if (pthread_timedjoin_np(t->tid, nullptr, &deadline) != 0) {
            out->status = -2;
            out->sim_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
            return -2;   // threads are left running; the caller must not continue
        }
    }
    out->sim_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    // solver() writes miscCounters[0..6]; [6] = 1 SAT / 0 UNSAT (left at the
    // page size, 16, on its error path).
    out->iterations = (uint32_t)d.misc[0];
    out->decisions = (uint32_t)d.misc[1];
    out->retries = (uint32_t)d.misc[2];
    out->backtracks = (uint32_t)d.misc[3];
    out->restarts = (uint32_t)d.misc[4];
    int flag = d.misc[6];
    out->status = flag == 1 ? 1 : flag == 0 ? 0 : -1;

    if (out->status == 1 && model) {
        // COPY_OUT: answerStack holds every variable's literal in trail order.
        std::memset(model, 0, (size_t)numVars);
        for (int i = 0; i < numVars; i++) {
            lit l = d.answerStack[i];
            int v = (l < 0 ? -l : l) - 1;
            if (v < 0 || v >= numVars || model[v] != 0) {
                out->status = -1;   // malformed or duplicate assignment
                break;
            }
            model[v] = l > 0 ? 1 : -1;
        }
    }
    return out->status;
}

extern "C" rl_solve_result rl_sataccel_solve(const sa_image *img, int8_t *model)
{
    rl_sataccel_result r;
    rl_solve_result s = {-1, 0, 0, 0};
    int st = rl_sataccel_solve_image(img, model, 600.0, &r);
    s.sat = st < 0 ? -1 : st;
    s.decisions = r.decisions;
    s.conflicts = r.backtracks;
    return s;
}
