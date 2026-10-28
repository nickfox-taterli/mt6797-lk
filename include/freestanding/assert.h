/* SPDX-License-Identifier: MIT */
#ifdef NDEBUG
#define assert(x) ((void)0)
#else
#error "Imported miniz is built with NDEBUG; all external inputs are checked by the caller"
#endif
