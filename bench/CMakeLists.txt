# Benchmarks: standalone executables measuring simulation performance.
# They are not run by CTest; launch them manually (ideally in a Release build).

add_executable(bench_pathfinding bench_pathfinding.c)
target_link_libraries(bench_pathfinding PRIVATE colony_core)
colony_configure_target(bench_pathfinding)
