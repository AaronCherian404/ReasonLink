/*
 * Declarations-only stand-in for <zlib.h>, used by `make riscv` only.
 *
 * MiniSat's utils/ParseUtils.h (pulled in via utils/Options.h) includes
 * <zlib.h> for its gzip DIMACS reader. The baselines never parse files with
 * MiniSat, so only these declarations are needed and nothing links against
 * zlib. RISC-V sysroots such as ESP's toolchain ship without zlib headers.
 */
#ifndef RL_COMPAT_ZLIB_H
#define RL_COMPAT_ZLIB_H

typedef struct gzFile_s *gzFile;
int gzread(gzFile file, void *buf, unsigned len);

#endif
