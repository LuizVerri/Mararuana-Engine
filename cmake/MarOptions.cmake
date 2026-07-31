# ==============================================================================
#  MarOptions.cmake — option declarations
# ------------------------------------------------------------------------------
#  Defines mar_option(), the one and only way to declare a configurable
#  option in this engine. It doesn't touch any target — it just exposes the
#  option in the cache (cmake-gui / ccmake / -D on the command line) and
#  registers it so it shows up in mar_print_config_summary(). Turning these
#  options into actual compiler flags is cmake/MarTargetConfig.cmake's job.
#
#  Every actual option lives in cmake/MararuanaOptions.cmake — that's the
#  file to open to configure the project. This file only defines the tool.
#
#  Syntax:
#    mar_option(<NAME> BOOL   <ON|OFF> "<description>")
#    mar_option(<NAME> STRING <default> "<description>" [STRINGS v1 v2 ...])
#
#  STRINGS turns the field into a dropdown in cmake-gui/ccmake instead of
#  free text — use it whenever the valid values are a closed set. On top of
#  the dropdown, the current value is actually VALIDATED against the list:
#  `-DMAR_SIMD_LEVEL=nonsense` on the command line (which bypasses the
#  dropdown) fails configuration with a clear error instead of silently
#  sailing through until something breaks mid-build.
#
#  Why function() and not macro():
#  inside a CMake macro(), ARGN only works through "${ARGN}" text
#  substitution — used bare in if(ARGN) or list(GET ARGN ...), it doesn't
#  see the extra arguments at all. It resolves to an ordinary (undefined)
#  variable called ARGN, so the if() is always false and the STRINGS block
#  never runs. function() doesn't have this trap: ARGN is a real local
#  variable there. option()/set(CACHE ...) stay global either way, so
#  switching to function() costs nothing.
# ==============================================================================

include_guard(GLOBAL)

# Global list of every option declared through mar_option(), used by
# mar_print_config_summary() (in MarTargetConfig.cmake) to print the
# current configuration at the end of `cmake -S . -B build`.
define_property(GLOBAL PROPERTY MAR_ALL_OPTIONS
    BRIEF_DOCS "List of every option declared via mar_option()"
    FULL_DOCS  "Populated automatically by mar_option(); consumed by mar_print_config_summary().")

function(mar_option name type default docstring)
    if("${type}" STREQUAL "BOOL")
        option(${name} "${docstring}" ${default})

    elseif("${type}" STREQUAL "STRING")
        set(${name} "${default}" CACHE STRING "${docstring}")

        # Optional trailing args: STRINGS v1 v2 ... -> dropdown in
        # cmake-gui/ccmake instead of a free-text field.
        if(ARGN)
            list(GET ARGN 0 _mar_opt_kw)
            if(NOT _mar_opt_kw STREQUAL "STRINGS")
                message(FATAL_ERROR
                    "mar_option(${name}): unexpected argument '${_mar_opt_kw}' (expected STRINGS)")
            endif()
            set(_mar_opt_choices ${ARGN})
            list(REMOVE_AT _mar_opt_choices 0)
            set_property(CACHE ${name} PROPERTY STRINGS ${_mar_opt_choices})

            # Actually validate the current value — also catches someone
            # hand-editing CMakeCache.txt or passing -D on the command line.
            list(FIND _mar_opt_choices "${${name}}" _mar_opt_index)
            if(_mar_opt_index EQUAL -1)
                string(REPLACE ";" ", " _mar_opt_choices_str "${_mar_opt_choices}")
                message(FATAL_ERROR
                    "mar_option(${name}): invalid value '${${name}}'. Accepted values: ${_mar_opt_choices_str}")
            endif()
        endif()

    else()
        message(FATAL_ERROR
            "mar_option(${name}): invalid type '${type}' — use BOOL or STRING")
    endif()

    set_property(GLOBAL APPEND PROPERTY MAR_ALL_OPTIONS ${name})
endfunction()