// See rl_nvdla.h. Mirrors NV_NVDLA/sw/umd/apps/runtime (RuntimeTest.cpp:
// load, initEMU, allocate/bind tensors, submit; TestUtils.cpp createImageCopy
// and DlaImage.cpp getAddrOffset for the tensor layouts), without the
// image-file round trips.
#include "rl_nvdla.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "nvdla/IRuntime.h"

struct rl_nvdla {
    nvdla::IRuntime *rt = nullptr;
    std::vector<NvU8> loadable;
    nvdla::IRuntime::NvDlaTensor in{}, out{};
    void *in_h = nullptr, *out_h = nullptr;
    NvU8 *in_buf = nullptr, *out_buf = nullptr;
};

// Byte offset of element (w, h, c) in an int8 tensor (DlaImage::getAddrOffset).
static long offset(const nvdla::IRuntime::NvDlaTensor &t, unsigned w, unsigned h, unsigned c)
{
    switch (t.pixelFormat) {
    case NVDLA_PIXEL_FORMAT_R8:
        return (long)h * t.stride[1] + w;
    case NVDLA_PIXEL_FORMAT_FEATURE:      // D_F8_CxHWx_x32_I
        return (long)(c / 32) * t.stride[2] + (long)h * t.stride[1] + w * 32 + c % 32;
    case NVDLA_PIXEL_FORMAT_FEATURE_X8:   // D_F8_CxHWx_x8_I
        return (long)(c / 8) * t.stride[2] + (long)h * t.stride[1] + w * 8 + c % 8;
    default:
        return -1;
    }
}

extern "C" rl_nvdla *rl_nvdla_open(const char *path, int instance)
{
    rl_nvdla *n = new rl_nvdla;
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "rl_nvdla: cannot open %s\n", path);
        delete n;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    n->loadable.resize(ftell(f));
    fseek(f, 0, SEEK_SET);
    size_t got = fread(n->loadable.data(), 1, n->loadable.size(), f);
    fclose(f);
    int nin = 0, nout = 0;
    const char *what = nullptr;
    if (got != n->loadable.size()) what = "short read of the loadable";
    else if (!(n->rt = nvdla::createRuntime())) what = "createRuntime";
    else if (!n->rt->load(n->loadable.data(), instance)) what = "load";
    else if (!n->rt->initEMU()) what = "initEMU";
    else if (n->rt->getNumInputTensors(&nin) != NvDlaSuccess || nin != 1) what = "input tensors";
    else if (n->rt->getNumOutputTensors(&nout) != NvDlaSuccess || nout != 1) what = "output tensors";
    else if (n->rt->getInputTensorDesc(0, &n->in) != NvDlaSuccess) what = "getInputTensorDesc";
    else if (n->rt->getOutputTensorDesc(0, &n->out) != NvDlaSuccess) what = "getOutputTensorDesc";
    else if (n->in.dataType != NVDLA_DATA_TYPE_INT8 || n->out.dataType != NVDLA_DATA_TYPE_INT8)
        what = "int8 tensors expected";
    else if (n->in.dims.w != 28 || n->in.dims.h != 28 || n->in.dims.c != 1 || n->out.dims.c < 10)
        what = "unexpected tensor shape (want 1x28x28 in, >= 10 channels out)";
    else if (offset(n->in, 0, 0, 0) < 0 || offset(n->out, 0, 0, 0) < 0) what = "unsupported tensor layout";
    else if (n->rt->allocateSystemMemory(&n->in_h, n->in.bufferSize, (void **)&n->in_buf) != NvDlaSuccess)
        what = "allocate input";
    else if (n->rt->allocateSystemMemory(&n->out_h, n->out.bufferSize, (void **)&n->out_buf) != NvDlaSuccess)
        what = "allocate output";
    else if (!n->rt->bindInputTensor(0, n->in_h) || !n->rt->bindOutputTensor(0, n->out_h)) what = "bind";
    if (what) {
        fprintf(stderr, "rl_nvdla: %s failed\n", what);
        rl_nvdla_close(n);
        return nullptr;
    }
    memset(n->in_buf, 0, n->in.bufferSize);
    return n;
}

extern "C" int rl_nvdla_classify(rl_nvdla *n, const uint8_t *px, int8_t *out)
{
    for (unsigned h = 0; h < 28; h++)
        for (unsigned w = 0; w < 28; w++)
            n->in_buf[offset(n->in, w, h, 0)] =
                (NvU8)(NvS8)std::floor(px[28 * h + w] * 127.0 / 255.0 + 0.5);
    if (!n->rt->submit()) return -1;
    for (unsigned c = 0; c < 10; c++) out[c] = (int8_t)n->out_buf[offset(n->out, 0, 0, c)];
    return 0;
}

extern "C" void *rl_nvdla_output(rl_nvdla *n, uint32_t *group, uint32_t *surf_stride)
{
    *group = n->out.pixelFormat == NVDLA_PIXEL_FORMAT_FEATURE_X8 ? 8 : 32;
    *surf_stride = n->out.stride[2];
    return n->out_buf + offset(n->out, 0, 0, 0);
}

extern "C" void rl_nvdla_close(rl_nvdla *n)
{
    if (!n) return;
    if (n->rt) {
        if (n->in_h) n->rt->freeSystemMemory(n->in_h, n->in.bufferSize);
        if (n->out_h) n->rt->freeSystemMemory(n->out_h, n->out.bufferSize);
        n->rt->stopEMU();
        n->rt->unload();
    }
    delete n;
}
