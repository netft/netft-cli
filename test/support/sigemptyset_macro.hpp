#pragma once

#include <signal.h>

#ifdef sigemptyset
#undef sigemptyset
#endif

inline int netft_test_sigemptyset(sigset_t *set) { return ::sigemptyset(set); }

#define sigemptyset(set) (netft_test_sigemptyset(set))
