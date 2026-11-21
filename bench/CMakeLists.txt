# Benchmarks: standalone executables measuring simulation performance.
# They are not run by CTest; launch them manually (ideally in a Release build).

add_executable(bench_pathfinding bench_pathfinding.c)
target_link_libraries(bench_pathfinding PRIVATE colony_core)
colony_configure_target(bench_pathfinding)

add_executable(bench_simulation bench_simulation.c)
target_link_libraries(bench_simulation PRIVATE colony_core)
colony_configure_target(bench_simulation)

add_executable(bench_vision bench_vision.c)
target_link_libraries(bench_vision PRIVATE colony_core)
colony_configure_target(bench_vision)

add_executable(bench_social bench_social.c)
target_link_libraries(bench_social PRIVATE colony_core)
colony_configure_target(bench_social)

add_executable(bench_water bench_water.c)
target_link_libraries(bench_water PRIVATE colony_core)
colony_configure_target(bench_water)
