#pragma once

#include <cstdint>

// Cross-compiler function/pointer attributes and branch hints.
//
// Each macro wraps the MSVC vs GCC/Clang spelling of the same hint behind
// one portable name, so call sites never branch on _MSC_VER / __GNUC__ /
// __clang__ directly.

#ifndef MAR_FORCEINLINE
#   if   defined(_MSC_VER)
#       define MAR_FORCEINLINE __forceinline
#   elif defined(__GNUC__) || defined(__clang__)
#       define MAR_FORCEINLINE __attribute__((always_inline)) inline
#   else
#       define MAR_FORCEINLINE inline
#   endif
#endif

#ifndef MAR_LIKELY
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_LIKELY(x)   __builtin_expect(!!(x), 1)
#       define MAR_UNLIKELY(x) __builtin_expect(!!(x), 0)
#   else
#       define MAR_LIKELY(x)   (x)
#       define MAR_UNLIKELY(x) (x)
#   endif
#endif

#ifndef MAR_ASSUME
#   if defined(__clang__) || defined(__GNUC__)
#       define MAR_ASSUME(cond) __attribute__((assume(cond)))
#   elif defined(_MSC_VER)
#       define MAR_ASSUME(cond) __assume(cond)
#   else
#       define MAR_ASSUME(cond) ((void)0)
#   endif
#endif

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

#ifndef MAR_COLD_FUNC
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_COLD_FUNC __attribute__((noinline, cold))
#   else
#       define MAR_COLD_FUNC
#   endif
#endif

#ifndef MAR_RESTRICT
#   if defined(__GNUC__) || defined(__clang__)
#       define MAR_RESTRICT __restrict__
#   elif defined(_MSC_VER)
#       define MAR_RESTRICT __restrict
#   else
#       define MAR_RESTRICT
#   endif
#endif