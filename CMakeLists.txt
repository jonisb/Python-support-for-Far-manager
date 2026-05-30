cmake_minimum_required(VERSION 3.14)
project(PythonFar)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ===== Loader Plugin (PythonFar.dll) =====
add_library(PythonFar SHARED
    src/loader/loader.cpp
    src/loader/interface.cpp
)

