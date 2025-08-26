#pragma once
#include <cassert>

#ifdef NDEBUG
    #define DEBUG_ASSERT(condition) ((void)0)
#else
    #define DEBUG_ASSERT(condition) assert(condition)
#endif
