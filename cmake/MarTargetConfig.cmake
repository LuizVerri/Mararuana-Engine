# ==============================================================================
#  MarTargetConfig.cmake — applying the options
# ------------------------------------------------------------------------------
#  Reads the MAR_* variables declared in MararuanaOptions.cmake and turns
#  them into real compiler flags for a specific target: compile
#  definitions, compile options, include directories, and the res/ folder
#  (via mar_target_resources(), in MarResources.cmake).
#
#  Doesn't declare any new options — just consumes what
#  MararuanaOptions.cmake exposed. The split is intentional:
#  MararuanaOptions.cmake is "what can be configured", this file is "what
#  that actually means in practice".
#
#  Usage: mar_configure_target(<target>)
#    Call this for every target that needs the engine's options (today:
#    Mararuana-Engine and Sandbox), always from inside the CMakeLists.txt
#    where that target was created — never from somewhere else. This
#    matters because the function relies on CMAKE_CURRENT_SOURCE_DIR to
#    find the target's own include/ and res/; calling it from another
#    directory resolves that path wrong — the engine would end up picking
#    up Sandbox's include/ instead of its own.
#
#    The option values themselves (MAR_ENABLE_WINDOW, MAR_SIMD_LEVEL, ...)
#    are identical across every target because they're declared once, in
#    cmake/MararuanaOptions.cmake — see the ODR note further down.
# ==============================================================================

include_guard(GLOBAL)

function(mar_configure_target target)
    if(NOT TARGET ${target})
        return()
    endif()

    target_include_directories(${target}
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )

    # ---- MAR_ENABLE_WINDOW --------------------------------------------------
    # The option name IS the compile definition — no separate MAR_WINDOW
    # macro. One name to search for, in CMake and in C++ alike.
    if(MAR_ENABLE_WINDOW)
        target_compile_definitions(${target} PUBLIC MAR_ENABLE_WINDOW)
    endif()

    # ---- MAR_CUSTOM_EVENTS_HEADER --------------------------------------------
    # EventBase.h expands EventType/EventCategory through
    # "#include MAR_CUSTOM_EVENTS_H" (an X-Macro). Every target compiling
    # EventBase.h needs to see the exact same header/macro, or the enum
    # diverges between translation units — an ODR violation — if the
    # engine and Sandbox are compiled separately. That's why
    # MAR_CUSTOM_EVENTS_HEADER is declared once (MararuanaOptions.cmake)
    # and this function always reads that same value for every target.
    target_compile_definitions(${target} PRIVATE
        MAR_CUSTOM_EVENTS_H=\"${MAR_CUSTOM_EVENTS_HEADER}\"
    )

    # ---- MAR_SIMD_LEVEL -------------------------------------------------------
    # The -m.../ /arch:... flags below still do the actual job of turning on
    # codegen for the ISA — that part is unchanged, and still the only thing
    # that can (compiler-native __SSE2__/__AVX2__ macros just report what the
    # flag already did, they don't drive anything themselves).
    #
    # What's new: MAR_ENABLE_SSE2 / MAR_ENABLE_AVX2, set explicitly below as
    # plain compile definitions. Headers should branch on these, not on
    # __SSE2__/__AVX2__ directly — MSVC never defines __SSE__/__SSE2__ at all
    # (no /arch: flag or bitness makes it appear), which silently pushed
    # MAR_SIMD_LEVEL=SSE2 onto the scalar memcpy fallback on MSVC even though
    # SSE2 was genuinely available and enabled. MSVC does define __AVX__/
    # __AVX2__ correctly, but using our own defines keeps both levels handled
    # the same way and keeps header code decoupled from per-compiler quirks.
    # As a bonus, MAR_SIMD_LEVEL=NONE now really means "no SIMD, ever" — even
    # on x86-64, where SSE2 is otherwise unconditionally present — since the
    # header no longer infers anything from the target architecture itself.
    if(NOT MAR_SIMD_LEVEL STREQUAL "NONE")
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|i[3-6]86)$")
            if(MAR_SIMD_LEVEL STREQUAL "AVX2")
                target_compile_options(${target} PUBLIC
                    $<$<CXX_COMPILER_ID:MSVC>:/arch:AVX2>
                    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-mavx2>
                )
                # AVX2 is a superset of SSE2 — both defines go out together,
                # so code paths that only need SSE2 can still use it.
                target_compile_definitions(${target} PUBLIC
                    MAR_ENABLE_SSE2
                    MAR_ENABLE_AVX2
                )
            elseif(MAR_SIMD_LEVEL STREQUAL "SSE2")
                # SSE2 is already the x86-64 baseline (GCC/Clang/MSVC all
                # define __SSE2__ with no flag at all; MSVC won't even
                # accept an explicit /arch:SSE2 on x64, since it's a no-op).
                # Only 32-bit targets actually need the flag spelled out.
                if(CMAKE_SIZEOF_VOID_P EQUAL 4)
                    target_compile_options(${target} PUBLIC
                        $<$<CXX_COMPILER_ID:MSVC>:/arch:SSE2>
                        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-msse2>
                    )
                else()
                    target_compile_options(${target} PUBLIC
                        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-msse2>
                    )
                endif()
                target_compile_definitions(${target} PUBLIC MAR_ENABLE_SSE2)
            else()
                # Shouldn't happen in practice — mar_option() already
                # validates MAR_SIMD_LEVEL against STRINGS at configure
                # time. Kept as a defense-in-depth fallback (e.g. a
                # hand-edited cache).
                message(WARNING
                    "Unknown MAR_SIMD_LEVEL: '${MAR_SIMD_LEVEL}' (use NONE, SSE2, or AVX2)")
            endif()
        else()
            message(STATUS
                "${target}: MAR_SIMD_LEVEL='${MAR_SIMD_LEVEL}' ignored — '${CMAKE_SYSTEM_PROCESSOR}' isn't x86")
        endif()
    endif()

    # ---- res/ -----------------------------------------------------------------
    # Wires up a res/ folder next to this target's include/, if one exists
    # (MAR_RES_DIR + post-build copy). No-op if it doesn't. See
    # cmake/MarResources.cmake.
    mar_target_resources(${target})
endfunction()

# ==============================================================================
#  mar_print_config_summary() — a readable configuration summary
# ------------------------------------------------------------------------------
#  Prints, in a single block at the end of `cmake -S . -B build`:
#    - every option declared via mar_option() (+ MARARUANA_APP_ROOT) and
#      its current value;
#    - each executable's final res/ layout (filled in by
#      mar_target_resources() in MarResources.cmake, whenever there's
#      something to report).
#  Called once, from the root CMakeLists.txt, after the add_subdirectory()
#  calls — purely informational, doesn't affect the build.
# ==============================================================================
function(mar_print_config_summary)
    get_property(_mar_opts GLOBAL PROPERTY MAR_ALL_OPTIONS)
    get_property(_mar_res_lines GLOBAL PROPERTY MAR_RESOURCE_SUMMARY_LINES)

    if(NOT _mar_opts AND NOT _mar_res_lines)
        return()
    endif()

    message(STATUS "")
    message(STATUS "==================== Mararuana — configuration ====================")

    if(_mar_opts)
        list(REMOVE_DUPLICATES _mar_opts)
        message(STATUS "  Options:")
        foreach(_mar_opt ${_mar_opts})
            message(STATUS "    ${_mar_opt} = ${${_mar_opt}}")
        endforeach()
    endif()

    if(_mar_res_lines)
        message(STATUS "  Resources (res/):")
        foreach(_mar_line ${_mar_res_lines})
            message(STATUS "${_mar_line}")
        endforeach()
    endif()

    message(STATUS "============================================================================")
    message(STATUS "")
endfunction()