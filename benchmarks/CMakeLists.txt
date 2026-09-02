# Checkpoint Store benchmarks
add_executable(benchmarks benchmarks.cpp)
target_link_libraries(benchmarks PRIVATE checkpointstore)
target_include_directories(benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/include)
