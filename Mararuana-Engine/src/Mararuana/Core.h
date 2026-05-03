#pragma once


#define BIT(x) (1 << (x))


// Force inlining -----------------------------------------------------------
#ifndef MAR_FORCEINLINE
#   if defined(_MSC_VER)
#       define MAR_FORCEINLINE __forceinline
#   elif defined(__GNUC__) || defined(__clang__)
#       define MAR_FORCEINLINE __attribute__((always_inline)) inline
#   else
#       define MAR_FORCEINLINE inline
#   endif
#endif
// -----------------------------------------------------------------------------


// Branch prediction -----------------------------------------------------------
#ifndef MAR_LIKELY
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_LIKELY(x)   __builtin_expect(!!(x), 1)
#       define MAR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#   else
#       define MAR_LIKELY(x)   (x)
#       define MAR_UNLIKELY(x) (x)
#   endif
#endif
// -----------------------------------------------------------------------------


// Assertions ------------------------------------------------------------------
#ifndef MAR_CORE_ASSERT
#   include <cassert>
#   define MAR_CORE_ASSERT(cond, ...) assert(cond)
#endif
// -----------------------------------------------------------------------------


// Aligned pointer hint -----------------------------------------------------------
#ifndef MAR_ASSUME_ALIGNED
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_ASSUME_ALIGNED(ptr, align) \
            reinterpret_cast<decltype(ptr)>(__builtin_assume_aligned((ptr), (align)))
#   elif defined(_MSC_VER)
#       define MAR_ASSUME_ALIGNED(ptr, align) \
            (__assume((reinterpret_cast<uintptr_t>(ptr) & ((align) - 1u)) == 0u), (ptr))
#   else
#       define MAR_ASSUME_ALIGNED(ptr, align) (ptr)
#   endif
#endif
// --------------------------------------------------------------------------------


// Cold paths ------------------------------------------------------------------
#ifndef MAR_COLD_FUNC
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_COLD_FUNC __attribute__((noinline, cold))
#   else
#       define MAR_COLD_FUNC
#   endif
#endif
// -----------------------------------------------------------------------------


// Restrict hint ---------------------------------------------------------------
#ifndef MAR_RESTRICT
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_RESTRICT __restrict__
#   elif defined(_MSC_VER)
#       define MAR_RESTRICT __restrict
#   else
#       define MAR_RESTRICT
#   endif
#endif
// -----------------------------------------------------------------------------


// Prefetch and pause - no ops -------------------------------------------------
// Kept for call sites only.
#define MAR_PREFETCH_T0(ptr)  ((void)0)
#define MAR_PREFETCH_T1(ptr)  ((void)0)
#define MAR_PREFETCH_NTA(ptr) ((void)0)
#define MAR_CPU_PAUSE()       ((void)0)

#include <config/Config.h>