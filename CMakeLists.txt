cmake_minimum_required(VERSION 3.5)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set extension name here
set(TARGET_NAME flockmtl)

set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

# Include directories
include_directories(src/include)
add_subdirectory(src)

# Find dependencies
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Build the DuckDB static and loadable extensions
build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

# Link the filesystem library for GCC < 9
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION
                                            VERSION_LESS 9.0)
  target_link_libraries(${EXTENSION_NAME} -lstdc++fs)
endif()

# Check if we're in debug mode and enable AddressSanitizer
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  message(STATUS "Enabling AddressSanitizer for Debug build")
  # Enable AddressSanitizer
  target_compile_options(${EXTENSION_NAME} PRIVATE -fsanitize=address
                                                   -fno-omit-frame-pointer)
  target_link_options(${EXTENSION_NAME} PRIVATE -fsanitize=address)

  target_compile_options(${LOADABLE_EXTENSION_NAME}
                         PRIVATE -fsanitize=address -fno-omit-frame-pointer)
  target_link_options(${LOADABLE_EXTENSION_NAME} PRIVATE -fsanitize=address)
endif()

# Link libraries for the static extension
target_link_libraries(${EXTENSION_NAME} CURL::libcurl
                      nlohmann_json::nlohmann_json)

# Link libraries for the loadable extension
target_link_libraries(${LOADABLE_EXTENSION_NAME} CURL::libcurl
                      nlohmann_json::nlohmann_json)

# Install the extension
install(
  TARGETS ${EXTENSION_NAME}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")

# ===============================
# Add Tests
# ===============================

# Define the test directory
set(TEST_DIR ${CMAKE_CURRENT_SOURCE_DIR}/test)

# Recursively find all test files in the test directory
file(GLOB_RECURSE TEST_SOURCES ${TEST_DIR}/*.cpp)

# Find Catch2
find_package(Catch2 REQUIRED)

# Add an executable for the tests
add_executable(${TARGET_NAME}_tests ${TEST_SOURCES})

# Include directories for tests
target_include_directories(${TARGET_NAME}_tests PRIVATE test/include)

# Link the test executable to the extension and the Catch2 framework (used for testing)
target_link_libraries(${TARGET_NAME}_tests PRIVATE ${EXTENSION_NAME} Catch2::Catch2WithMain)

# Register the tests with CTest (built into CMake)
enable_testing()
add_test(NAME ${TARGET_NAME}_tests COMMAND ${TARGET_NAME}_tests)