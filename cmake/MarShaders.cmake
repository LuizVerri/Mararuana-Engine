# ==============================================================================
#  MarShaders.cmake — GLSL -> SPIR-V shader compilation
# ------------------------------------------------------------------------------
#  Defines mar_compile_shaders(<target> <source_dir> <res_dir>):
#
#    Recursively finds every shader under <source_dir> — any depth, any
#    number of subfolders — and compiles each one to SPIR-V with glslc,
#    mirroring the same subfolder layout under <res_dir>/shaders/. The
#    result is registered as an extra resource of <target> (see
#    mar_target_extra_resources() in MarResources.cmake), so it merges
#    into any executable's final res/ automatically. Nothing to list by
#    hand: drop a new shader anywhere under <source_dir> and it gets
#    picked up.
#
#  Recognized shader stages (by extension): .vert .frag .comp .geom .tesc
#  .tese — add more file(GLOB_RECURSE ...) patterns below if needed. A
#  shared include file (e.g. common.glsl) is fine to keep alongside them;
#  it's simply not one of the recognized extensions, so it won't be sent
#  to glslc as its own translation unit.
#
#  Note: CONFIGURE_DEPENDS makes the Ninja and Makefile generators
#  re-check <source_dir> on every build, so a newly added shader file is
#  picked up without manually re-running `cmake -S . -B build`. The
#  Visual Studio and Xcode generators don't support this — on those, add
#  a new shader and reconfigure once by hand.
# ==============================================================================

include_guard(GLOBAL)

function(mar_compile_shaders target source_dir res_dir)
    if(NOT TARGET ${target})
        return()
    endif()

    if(NOT IS_DIRECTORY "${source_dir}")
        message(STATUS "${target}: no shader directory at '${source_dir}', skipping shader compilation")
        return()
    endif()

    find_program(GLSLC_EXECUTABLE glslc)
    if(NOT GLSLC_EXECUTABLE)
        message(FATAL_ERROR "glslc not found! Install the Vulkan SDK and make sure it's on PATH.")
    endif()

    file(GLOB_RECURSE _mar_shader_files
        RELATIVE "${source_dir}"
        CONFIGURE_DEPENDS
        "${source_dir}/*.vert"
        "${source_dir}/*.frag"
        "${source_dir}/*.comp"
        "${source_dir}/*.geom"
        "${source_dir}/*.tesc"
        "${source_dir}/*.tese"
    )

    if(NOT _mar_shader_files)
        message(STATUS "${target}: no shader files found under '${source_dir}'")
        return()
    endif()

    set(_mar_shader_output_dir "${res_dir}/shaders")
    set(_mar_spirv_outputs "")

    foreach(_mar_shader ${_mar_shader_files})
        set(_mar_shader_source "${source_dir}/${_mar_shader}")
        set(_mar_spirv_output  "${_mar_shader_output_dir}/${_mar_shader}.spv")

        get_filename_component(_mar_spirv_output_subdir "${_mar_spirv_output}" DIRECTORY)
        file(MAKE_DIRECTORY "${_mar_spirv_output_subdir}")

        add_custom_command(
            OUTPUT  "${_mar_spirv_output}"
            COMMAND "${GLSLC_EXECUTABLE}" "${_mar_shader_source}" -o "${_mar_spirv_output}"
            DEPENDS "${_mar_shader_source}"
            COMMENT "Compiling shader: ${_mar_shader} -> SPIR-V"
            VERBATIM
        )

        list(APPEND _mar_spirv_outputs "${_mar_spirv_output}")
    endforeach()

    add_custom_target(${target}CompileShaders ALL
        DEPENDS ${_mar_spirv_outputs}
    )

    # Ensures shaders are compiled as part of building the engine, and —
    # transitively, through Sandbox linking the engine — before Sandbox's
    # post-build res/ copy runs.
    add_dependencies(${target} ${target}CompileShaders)

    mar_target_extra_resources(${target} "${res_dir}")
endfunction()