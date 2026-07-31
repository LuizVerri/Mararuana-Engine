// ==============================================================================
//  CustomEvents.h — App-level event registry (X-Macro list)
// ------------------------------------------------------------------------------
//  Single place where this app registers its events with the engine.
//  EventBase.h includes this file twice, with different macros defined each
//  time (once to expand EventType values, once to expand EventCategory
//  flags) — so this file must NOT have an include guard / #pragma once.
//
//  To add a new event:
//    1. Create include/CustomEvents/YourEvent.h following the pattern in
//       ItemPickupEvent.h — an #ifdef DEFINE_CATEGORIES branch for its
//       category, and a DEFINE_EVENT(...) branch for the event itself.
//    2. #include it below.
//
//  This file's own path is registered as MAR_CUSTOM_EVENTS_H entirely from
//  CMakeLists.txt (MAR_CUSTOM_EVENTS_HEADER variable) — nothing to touch
//  here or in any app-level config header for that.
// ==============================================================================

#include "CustomEvents/ItemPickupEvent.h"
// #include "CustomEvents/AnotherFutureEvent.h"