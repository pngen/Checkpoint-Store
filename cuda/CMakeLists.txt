# CUDA restore-to-device proof. Compiled only when CHECKPOINTSTORE_ENABLE_CUDA_PROOF=ON.
# This is NOT GPUDirect Storage: it is an ordinary host-to-device (H2D) copy
# after a verified CPU restore, followed by a real transform kernel, D2H, and a
# CPU reference comparison.
add_executable(cuda_proof cuda_proof.cu)
target_link_libraries(cuda_proof PRIVATE checkpointstore)
target_include_directories(cuda_proof PRIVATE ${CMAKE_SOURCE_DIR}/include)
set_target_properties(cuda_proof PROPERTIES CUDA_STANDARD 17)
