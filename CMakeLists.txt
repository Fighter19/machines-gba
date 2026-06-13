cmake_minimum_required(VERSION 3.28)
project(machines-gba)

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

add_library(sg_physics_2d STATIC ${SG_SOURCES})
target_include_directories(sg_physics_2d PUBLIC ${SG_PHYSICS_DIR})
add_executable(sg_physics_test tests/sg_physics_basic_test.cpp)
target_link_libraries(sg_physics_test sg_physics_2d)

add_executable(ppu_test tests/ppu_test.cpp)

if (USE_SDL)
    find_package(SDL2)
    target_link_libraries(sg_physics_test SDL2)
    target_compile_definitions(sg_physics_test PRIVATE USE_SDL)

    target_link_libraries(ppu_test SDL2)
    target_compile_definitions(ppu_test PRIVATE USE_SDL)
endif(USE_SDL)