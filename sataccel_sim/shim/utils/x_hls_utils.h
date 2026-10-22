// reg(x) inserts a pipeline register in HLS; identity in software.
#ifndef RL_SHIM_X_HLS_UTILS_H
#define RL_SHIM_X_HLS_UTILS_H
template <typename T>
T reg(T x)
{
    return x;
}
#endif
