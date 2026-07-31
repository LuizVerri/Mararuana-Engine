#pragma once

// General-purpose helper macros shared across the engine.

// Bitmask with bit `x` set — handy for flag-style enums:
//   enum Flags { FlagA = BIT(0), FlagB = BIT(1), FlagC = BIT(2) };
#define BIT(x) (1 << x)