#pragma once

// Mararuana.h
// Single include for client applications. Pull in this header and nothing else.
//
// What it provides:
//   - Logging (Log.h + macros)
//   - Event system (EventSystem.h)
//   - Application base class (Application.h)
//   - Program entry point (EntryPoint.h defines main)
//
// Note: EntryPoint.h must be included exactly once per executable.
// Because this header is the intended single point of inclusion, that
// constraint is satisfied automatically as long as client code uses only this file.

#include <stdio.h>
#include "Mararuana/Log.h"
#include "Mararuana/EventSystem/EventSystem.h"
#include "Mararuana/Application.h"

#include "Mararuana/EntryPoint.h"