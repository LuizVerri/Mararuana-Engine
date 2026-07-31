# ==============================================================================
#  MarResources.cmake — the res/ folder for each target
# ------------------------------------------------------------------------------
#  Declares two functions:
#
#    mar_target_resources(<target>)
#      Called automatically by mar_configure_target() (see
#      MarTargetConfig.cmake) for every target the engine configures. You
#      shouldn't need to call this by hand.
#
#    mar_target_extra_resources(<target> <dir>)
#      Called manually to register a directory GENERATED at build time
#      (not checked into res/) that should get merged in alongside the
#      normal resources when everything gets copied — today, only by
#      mar_compile_shaders() (see MarShaders.cmake), for the compiled
#      shaders. <dir> is copied whole, recursively, into the output res/
#      — so if <dir> contains "shaders/foo.spv", it ends up at
#      "res/shaders/foo.spv". Can be called more than once per target.
#
#  The "normal" res/ convention: if a "res/" folder exists next to a
#  target's "include/" (${CMAKE_CURRENT_SOURCE_DIR}/res —
#  mar_target_resources() always runs from inside the CMakeLists.txt where
#  the target was created), it gets wired up automatically. If it doesn't
#  exist, that part is just skipped — no target is required to have its
#  own res/.
#
#  What "wired up" means depends on the target type:
#
#   - ANY target with its own res/ gets the MAR_RES_DIR compile-time macro,
#     pointing at the absolute path of that res/ folder in the source
#     tree. Handy in development, when the binary runs straight out of the
#     build folder (e.g. `std::ifstream(MAR_RES_DIR "textures/foo.png")`).
#
#   - EXECUTABLE (e.g. Sandbox): gathers everything into one final res/
#     next to the binary (via a post-build command) — its own res/, its
#     own extras, the res/ of every linked library (the engine), and their
#     extras too (the engine's compiled shaders). This is what makes the
#     build "portable": copy just the bin/ folder to another machine and
#     it still works. It's also the only place that prints anything —
#     libraries only REGISTER what they have; the executable is the one
#     that actually assembles and reports the final res/, since that's the
#     only place a "final res/" really exists. See
#     mar_print_config_summary() in MarTargetConfig.cmake for where that
#     report gets printed.
#
#   - LIBRARY (e.g. Mararuana-Engine): a .lib/.a has no runtime location of
#     its own, so there's no post-build copy — it just registers its paths
#     (res/ and extras) for whichever executable links it.
# ==============================================================================

include_guard(GLOBAL)

# Target properties used to propagate a library's resources to whatever
# executable links it (e.g. Sandbox inheriting the engine's res/ and its
# compiled shaders).
define_property(TARGET PROPERTY MAR_RESOURCE_DIR
    BRIEF_DOCS "Absolute path to this target's res/ folder (source tree)"
    FULL_DOCS  "Set by mar_target_resources() when a res/ folder is found next to the target's include/.")

define_property(TARGET PROPERTY MAR_EXTRA_RESOURCE_DIRS
    BRIEF_DOCS "This target's build-time generated resource directories"
    FULL_DOCS  "Populated by mar_target_extra_resources(). Each directory gets merged into the final res/ during the same post-build copy as the source res/.")

# Ready-to-print lines for mar_print_config_summary() — one per executable
# with a non-empty final res/. Only filled in at the end of
# mar_target_resources(), never via loose messages scattered through
# configure.
define_property(GLOBAL PROPERTY MAR_RESOURCE_SUMMARY_LINES
    BRIEF_DOCS "Per-executable res/ summary lines"
    FULL_DOCS  "Populated by mar_target_resources(). Consumed by mar_print_config_summary().")

function(mar_target_extra_resources target dir)
    if(NOT TARGET ${target})
        return()
    endif()
    set_property(TARGET ${target} APPEND PROPERTY MAR_EXTRA_RESOURCE_DIRS "${dir}")
endfunction()

function(mar_target_resources target)
    if(NOT TARGET ${target})
        return()
    endif()

    set(_mar_res_dir "${CMAKE_CURRENT_SOURCE_DIR}/res")
    set(_mar_has_own_res FALSE)

    if(IS_DIRECTORY "${_mar_res_dir}")
        set(_mar_has_own_res TRUE)
        set_target_properties(${target} PROPERTIES MAR_RESOURCE_DIR "${_mar_res_dir}")

        # Absolute path available in C++, handy in development when the
        # binary runs straight from the build tree. The trailing slash
        # makes concatenation read naturally: MAR_RES_DIR "shaders/foo.spv".
        target_compile_definitions(${target} PRIVATE
            MAR_RES_DIR=\"${_mar_res_dir}/\"
        )
    endif()

    # Libraries (the engine) just register what they have — the property
    # above — for the final executable to propagate. Nothing more to do here.
    get_target_property(_mar_target_type ${target} TYPE)
    if(NOT _mar_target_type STREQUAL "EXECUTABLE")
        return()
    endif()

    # From here on, target is an executable: gather everything that needs
    # to land in the final res/ — its own, plus whatever it inherits from
    # linked libraries.
    set(_mar_sources "")

    if(_mar_has_own_res)
        list(APPEND _mar_sources "${_mar_res_dir}")
    endif()

    get_target_property(_mar_own_extra ${target} MAR_EXTRA_RESOURCE_DIRS)
    if(_mar_own_extra)
        list(APPEND _mar_sources ${_mar_own_extra})
    endif()

    get_target_property(_mar_libs ${target} LINK_LIBRARIES)
    if(_mar_libs)
        foreach(_mar_lib ${_mar_libs})
            if(TARGET ${_mar_lib})
                get_target_property(_mar_lib_res ${_mar_lib} MAR_RESOURCE_DIR)
                if(_mar_lib_res)
                    list(APPEND _mar_sources "${_mar_lib_res}")
                endif()

                get_target_property(_mar_lib_extra ${_mar_lib} MAR_EXTRA_RESOURCE_DIRS)
                if(_mar_lib_extra)
                    list(APPEND _mar_sources ${_mar_lib_extra})
                endif()
            endif()
        endforeach()
    endif()

    if(NOT _mar_sources)
        return()
    endif()

    set(_mar_out_res "$<TARGET_FILE_DIR:${target}>/res")
    set(_mar_sources_rel "")

    foreach(_mar_src ${_mar_sources})
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${_mar_src}" "${_mar_out_res}"
            COMMENT "[${target}] copying resources from '${_mar_src}'"
            VERBATIM
        )

        # Path relative to the workspace root just to keep the summary
        # readable — nobody wants /home/user/.../ repeated on every line.
        file(RELATIVE_PATH _mar_src_rel "${CMAKE_SOURCE_DIR}" "${_mar_src}")
        list(APPEND _mar_sources_rel "${_mar_src_rel}")
    endforeach()

    list(JOIN _mar_sources_rel ", " _mar_sources_str)
    set_property(GLOBAL APPEND PROPERTY MAR_RESOURCE_SUMMARY_LINES
        "    ${target}/res/  <-  ${_mar_sources_str}")
endfunction()