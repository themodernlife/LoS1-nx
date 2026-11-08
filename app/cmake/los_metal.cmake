# Native Metal support uses SPIR-V only as a shader intermediate. Rendering
# submits Metal command buffers directly and never calls MoltenVK.
# Reuse the SDK's pinned SPIRV-Cross dependency and avoid a second copy.
if(NOT TARGET spirv-cross-msl)
    message(FATAL_ERROR "Native Metal requires the SDK's spirv-cross-msl target")
endif()

# Apply the same tracked patch used by fetch_renut_deps.py to existing checkouts.
find_package(Git REQUIRED)
set(metal_patch "${CMAKE_SOURCE_DIR}/../patches/renut-deps/plume-native-metal.patch")
set(plume_source "${CMAKE_SOURCE_DIR}/thirdparty/plume")
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --check "${metal_patch}"
    WORKING_DIRECTORY "${plume_source}" RESULT_VARIABLE patch_needed OUTPUT_QUIET ERROR_QUIET)
if(patch_needed EQUAL 0)
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply "${metal_patch}"
        WORKING_DIRECTORY "${plume_source}" COMMAND_ERROR_IS_FATAL ANY)
else()
    execute_process(COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${metal_patch}"
        WORKING_DIRECTORY "${plume_source}" RESULT_VARIABLE patch_present OUTPUT_QUIET ERROR_QUIET)
    if(NOT patch_present EQUAL 0)
        message(FATAL_ERROR "plume-native-metal.patch does not match the installed plume. Re-fetch pinned Renut dependencies.")
    endif()
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${metal_patch}")

add_library(los_metal_backend STATIC
    "${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/backend/metal_backend.cpp")
target_include_directories(los_metal_backend PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/thirdparty/plume/contrib/metal-cpp")
target_link_libraries(los_metal_backend PUBLIC plume PRIVATE spirv-cross-msl spirv-cross-glsl spirv-cross-core)
target_compile_options(los_metal_backend PRIVATE -fblocks)

add_executable(los_metal_smoke EXCLUDE_FROM_ALL "${CMAKE_SOURCE_DIR}/../tools/macos/metal_smoke.cpp")
target_link_libraries(los_metal_smoke PRIVATE los_metal_backend)
target_include_directories(los_metal_smoke PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/metal_smoke")
foreach(stage vs ps)
    set(profile "${stage}_6_0")
    set(src "${CMAKE_SOURCE_DIR}/../tools/macos/metal_smoke_${stage}.hlsl")
    set(out "${CMAKE_BINARY_DIR}/metal_smoke/metal_smoke_${stage}.h")
    set(flags -spirv -fvk-use-dx-layout)
    if(stage STREQUAL "vs")
        list(APPEND flags -fvk-invert-y)
    endif()
    add_custom_command(OUTPUT "${out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/metal_smoke"
        COMMAND ${RENUT_DXC} -T ${profile} -E main ${flags} -Fh "${out}" -Vn "smoke_${stage}" "${src}"
        DEPENDS "${src}" VERBATIM)
    target_sources(los_metal_smoke PRIVATE "${out}")
endforeach()

# Exercise the same adaptive blur helper that the game shader includes, through
# the real DXC -> SPIR-V -> Metal path and the existing smoke-test bindings.
set(blur_smoke_src "${CMAKE_SOURCE_DIR}/../tools/macos/metal_blur_smoke_ps.hlsl")
set(blur_helper_dir "${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/shaders")
set(blur_smoke_out "${CMAKE_BINARY_DIR}/metal_smoke/metal_blur_smoke_ps.h")
add_custom_command(OUTPUT "${blur_smoke_out}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/metal_smoke"
    COMMAND ${RENUT_DXC} -T ps_6_0 -E main -spirv -fvk-use-dx-layout
        -I "${blur_helper_dir}" -Fh "${blur_smoke_out}" -Vn smoke_blur_ps "${blur_smoke_src}"
    DEPENDS "${blur_smoke_src}" "${blur_helper_dir}/motion_blur_filter.hlsli" VERBATIM)
target_sources(los_metal_smoke PRIVATE "${blur_smoke_out}")
