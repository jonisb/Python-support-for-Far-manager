cmake_minimum_required(VERSION 3.14)
project(PythonFar)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== Loader Plugin (PythonFar.dll) =====
add_library(PythonFar SHARED
    src/loader/loader.cpp
    src/loader/interface.cpp
)

# ===== Tests =====
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(pythonfar_tests
    tests/test_main.cpp
)
target_include_directories(pythonfar_tests PRIVATE)
target_link_libraries(pythonfar_tests
    gtest_main
)

add_test(NAME PythonFarTests 
         COMMAND pythonfar_tests
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:pythonfar_tests>")

