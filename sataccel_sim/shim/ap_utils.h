// ap_wait() marks a clock boundary in HLS; it is a no-op in software.
#ifndef RL_SHIM_AP_UTILS_H
#define RL_SHIM_AP_UTILS_H
inline void ap_wait() {}
inline void ap_wait_n(int) {}
#endif
