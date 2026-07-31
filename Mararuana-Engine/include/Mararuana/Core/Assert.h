#pragma once

#include <cassert>
#include <cstdlib>

#include "Mararuana/Core/Compiler.h"
#include "Mararuana/Log.h"

// Engine-side assertion macro.
//
// The previous version just forwarded to assert(cond) and silently
// discarded the message argument. That broke two things:
//
//   1) The diagnostic string written at the call site never reached any
//      output — accepted syntactically, never used.
//   2) In NDEBUG (release) builds, assert() compiles away entirely, so the
//      condition itself is never evaluated. Any call site relying on the
//      check to stop execution before doing something unsafe (e.g.
//      "file != nullptr" right before dereferencing it) would silently
//      continue into undefined behavior in release instead of stopping.
//
// Now the message is always logged through Mar::Log — including in
// release, where the log line is the only trace left before the program
// exits — and only the "how do we stop" behavior differs between configs
// (debugger break vs. fail-fast abort).
//
// NOTE: Mar::Log::Init() must run before the first MAR_CORE_ASSERT call
// anywhere in the engine, since the macro logs through
// Log::GetCoreLogger(). Firing an assert before Init() crashes inside the
// logger itself instead of reporting the original failure.
//
// NOTE 2: this now pulls Log.h (and <spdlog/spdlog.h>) into every TU that
// includes Core.h — i.e. most of the engine. That coupling was already
// implied by this file's original comment; just flagging the compile-time
// cost since it wasn't being paid before.
#ifndef MAR_CORE_ASSERT
#   ifdef NDEBUG
#       define MAR_CORE_ASSERT(cond, ...)                              \
            do {                                                        \
                if (MAR_UNLIKELY(!(cond))) {                            \
                    MAR_CORE_FATAL(__VA_ARGS__);                        \
                    std::abort();                                       \
                }                                                        \
            } while (0)
#   else
#       define MAR_CORE_ASSERT(cond, ...)                              \
            do {                                                        \
                const bool marAssertResult_ = static_cast<bool>(cond);  \
                if (MAR_UNLIKELY(!marAssertResult_)) {                  \
                    MAR_CORE_FATAL(__VA_ARGS__);                        \
                    assert(marAssertResult_);                           \
                }                                                        \
            } while (0)
#   endif
#endif