cmake_minimum_required(VERSION 3.24)
project(machines-gba)

set(USE_ASAN OFF CACHE BOOL "Using Address Sanitizer")
set(USE_VENDORED ON CACHE BOOL "Use vendored library")

if (USE_ASAN)
add_compile_options(-fsanitize=address)
add_link_options(-fsanitize=address)
endif(USE_ASAN)

set(CMAKE_CXX_STANDARD 17)

# Required for the visual test
set(USE_SDL ON CACHE BOOL "Use SDL for rendering (headless otherwise)")

set(SG_SOURCES
    sg_fixed_number_internal.cpp
    sg_fixed_vector2_internal.cpp
    sg_fixed_transform_2d_internal.cpp
    sg_shapes_2d_internal.cpp
    sg_broadphase_2d_internal.cpp
    sg_bodies_2d_internal.cpp
    sg_collision_detector_2d_internal.cpp
    sg_world_2d_internal.cpp
)

set(SG_PHYSICS_DIR "extern/sg-physics-2d/src/sg_physics_2d/internal/")

list(TRANSFORM SG_SOURCES PREPEND ${SG_PHYSICS_DIR})

if (FALSE)
add_library(sg_physics_2d STATIC ${SG_SOURCES})
target_include_directories(sg_physics_2d PUBLIC ${SG_PHYSICS_DIR})
add_executable(sg_physics_test tests/sg_physics_basic_test.cpp)
target_link_libraries(sg_physics_test sg_physics_2d)
endif ()

add_executable(ppu_test tests/ppu_test.cpp)

if (USE_SDL)

    # Use vendoered?
    if (USE_VENDORED)
        add_subdirectory(extern/SDL EXCLUDE_FROM_ALL)
    else()
        find_package(SDL3)
    endif()

if (FALSE)
    target_link_libraries(sg_physics_test SDL3::SDL3)
    target_compile_definitions(sg_physics_test PRIVATE USE_SDL)
endif()

    target_link_libraries(ppu_test SDL3::SDL3)
    target_compile_definitions(ppu_test PRIVATE USE_SDL)
endif(USE_SDL)