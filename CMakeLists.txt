cmake_minimum_required(VERSION 3.24)

project(CheckpointStore
  VERSION 1.0.0
  LANGUAGES CXX
  DESCRIPTION "Checkpoint-specific physical storage runtime")

# --------------------------------------------------------------------------
# Options
# --------------------------------------------------------------------------
option(CHECKPOINTSTORE_BUILD_TESTS "Build the Checkpoint Store test suite" ON)
option(CHECKPOINTSTORE_BUILD_EXAMPLES "Build the Checkpoint Store examples" ON)
option(CHECKPOINTSTORE_BUILD_BENCHMARKS "Build the Checkpoint Store benchmarks" ON)
option(CHECKPOINTSTORE_ENABLE_SYNTHETIC_BACKENDS
       "Enable deterministic synthetic remote/object backends" ON)
option(CHECKPOINTSTORE_ENABLE_CUDA_PROOF
       "Build the optional CUDA restore-to-device proof" OFF)
option(CHECKPOINTSTORE_INSTALL "Install Checkpoint Store" ON)
option(CHECKPOINTSTORE_CPACK "Enable CPack packaging" ON)

# --------------------------------------------------------------------------
# Compiler setup
# --------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Warn as error, zero-warning policy for project sources.
set(checkpointstore_extra_defs "")

if(MSVC)
  add_compile_options(/W4 /WX /permissive- /EHsc /Zc:__cplusplus /utf-8)
  # 4324: structure padded due to alignment specifier (benign in value types)
  # 4514: unreferenced inline function was removed (generated code)
  add_compile_options(/wd4324 /wd4514)
  set(checkpointstore_extra_defs
      NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# --------------------------------------------------------------------------
# The library
# --------------------------------------------------------------------------
add_library(checkpointstore STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/identity.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/hash.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/error.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/model.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/binformat.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/chunking.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/backend.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/local_backend.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthetic_backend.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/dedup.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/store.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/persistence.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/retention.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/gc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/restore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/protocol.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/replication.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/explain.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/accounting.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/tier.cpp
)

add_library(CheckpointStore::checkpointstore ALIAS checkpointstore)

target_include_directories(checkpointstore
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_definitions(checkpointstore
  PUBLIC $<BUILD_INTERFACE:${checkpointstore_extra_defs}>
)
target_compile_features(checkpointstore PUBLIC cxx_std_20)

if(WIN32)
  target_link_libraries(checkpointstore PUBLIC ws2_32 wsock32)
  target_link_libraries(checkpointstore PRIVATE bcrypt)
endif()

if(CHECKPOINTSTORE_ENABLE_SYNTHETIC_BACKENDS)
  target_compile_definitions(checkpointstore PUBLIC CHECKPOINTSTORE_SYNTHETIC_BACKENDS=1)
endif()

# --------------------------------------------------------------------------
# Install / package
# --------------------------------------------------------------------------
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(TARGETS checkpointstore
  EXPORT CheckpointStoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/checkpointstore
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT CheckpointStoreTargets
  FILE CheckpointStoreTargets.cmake
  NAMESPACE CheckpointStore::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CheckpointStore
)

configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckpointStoreConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/CheckpointStoreConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CheckpointStore
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/CheckpointStoreConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/CheckpointStoreConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/CheckpointStoreConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CheckpointStore
)

# --------------------------------------------------------------------------
# Subdirectories
# --------------------------------------------------------------------------
if(CHECKPOINTSTORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
if(CHECKPOINTSTORE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
if(CHECKPOINTSTORE_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()
add_subdirectory(cli)
add_subdirectory(tools)

if(CHECKPOINTSTORE_ENABLE_CUDA_PROOF)
  enable_language(CUDA)
  add_subdirectory(cuda)
endif()

if(CHECKPOINTSTORE_CPACK)
  include(CPack)
  set(CPACK_PACKAGE_VENDOR "Summon Software Labs")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "Checkpoint-specific physical storage runtime")
  set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
  set(CPACK_GENERATOR "ZIP")
endif()