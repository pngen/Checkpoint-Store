# Checkpoint Store multiprocess authority tools
add_executable(checkpoint-store-coordinator coordinator.cpp)
target_link_libraries(checkpoint-store-coordinator PRIVATE checkpointstore ws2_32 wsock32)
target_include_directories(checkpoint-store-coordinator PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(checkpoint-store-worker worker.cpp)
target_link_libraries(checkpoint-store-worker PRIVATE checkpointstore ws2_32 wsock32)
target_include_directories(checkpoint-store-worker PRIVATE ${CMAKE_SOURCE_DIR}/include)
