# Checkpoint Store CLI
add_executable(checkpoint-store cli.cpp)
target_link_libraries(checkpoint-store PRIVATE checkpointstore)
target_include_directories(checkpoint-store PRIVATE ${CMAKE_SOURCE_DIR}/include)
