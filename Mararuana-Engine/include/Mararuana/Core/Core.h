#pragma once

// Core.h
//
// Umbrella header kept only so existing `#include "Core.h"` call sites keep
// working unchanged. It used to hold everything below directly; now it just
// pulls the pieces together. New code is encouraged to include just the
// specific header it needs instead of this whole umbrella.
//
//   Version.h     — engine/app version + name macros
//   Definitions.h — small general-purpose macros (BIT, ...)
//   Compiler.h    — cross-compiler inline/branch/alignment hints
//   Assert.h      — MAR_CORE_ASSERT
//   Intrinsics.h  — CPU prefetch and pause/yield hints

#include "Mararuana/Core/Version.h"
#include "Mararuana/Core/Definitions.h"
#include "Mararuana/Core/Compiler.h"
#include "Mararuana/Core/Assert.h"
#include "Mararuana/Core/Intrinsics.h"