cmake_minimum_required(VERSION 3.14)
project(PythonFar)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Prefer a self-contained Python SDK downloaded by setup_python.ps1.
# This guarantees the adapter, test exe, and python_runtime all use the EXACT
# same Python build (DLL + stdlib), independent of any system Python.
if(NOT DEFINED PYTHONFAR_PYTHON_SDK)
    set(PYTHONFAR_PYTHON_SDK "${CMAKE_SOURCE_DIR}/build/python_sdk")
endif()
if(EXISTS "${PYTHONFAR_PYTHON_SDK}/include/Python.h")
    message(STATUS "Using self-contained Python SDK: ${PYTHONFAR_PYTHON_SDK}")
    set(Python3_ROOT_DIR "${PYTHONFAR_PYTHON_SDK}")
    set(Python3_FIND_STRATEGY LOCATION)
    set(Python3_FIND_REGISTRY NEVER)
endif()

# Detect cross-compilation: when building for a target whose python.exe cannot
# run on the host (e.g. building ARM64 binaries on an x64 runner), we must NOT
# require the Interpreter component (CMake would try to execute it) and we must
# skip running host-incompatible POST_BUILD tools (struct_dumper).
set(PYTHONFAR_CROSS_BUILD OFF)
if(CMAKE_GENERATOR_PLATFORM AND CMAKE_HOST_SYSTEM_PROCESSOR)
    string(TOLOWER "${CMAKE_GENERATOR_PLATFORM}" _pf_target)
    string(TOLOWER "${CMAKE_HOST_SYSTEM_PROCESSOR}" _pf_host)
    # Normalize host processor names to the platform vocabulary.
    if(_pf_host MATCHES "amd64|x86_64")
        set(_pf_host "x64")
    elseif(_pf_host MATCHES "arm64|aarch64")
        set(_pf_host "arm64")
    elseif(_pf_host MATCHES "x86|i.86")
        set(_pf_host "win32")
    endif()
    # x86 (Win32) binaries still run on x64 hosts, so that is not a cross build.
    if(NOT _pf_target STREQUAL _pf_host AND NOT (_pf_target STREQUAL "win32" AND _pf_host STREQUAL "x64"))
        set(PYTHONFAR_CROSS_BUILD ON)
        message(STATUS "Cross-build detected: target=${_pf_target} host=${_pf_host} (Interpreter optional, host tools skipped)")
    endif()
endif()

# Find Python. Development is always required; the Interpreter is only required
# for native builds (it cannot run when cross-compiling).
if(PYTHONFAR_CROSS_BUILD)
    find_package(Python3 REQUIRED COMPONENTS Development)
else()
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
endif()

# Add include directories
include_directories(include)
include_directories(${Python3_INCLUDE_DIRS})

# ===== Loader Plugin (PythonFar.dll) =====
add_library(PythonFar SHARED
    src/loader/loader.cpp
    src/loader/interface.cpp
    src/loader/dllmain.cpp
    src/logger.cpp
)
target_link_libraries(PythonFar ${Python3_LIBRARIES})
set_target_properties(PythonFar PROPERTIES
    PREFIX ""
    OUTPUT_NAME "PythonFar"
    LINK_FLAGS "/EXPORT:GetGlobalInfoW /EXPORT:SetStartupInfoW /EXPORT:GetPluginInfoW /EXPORT:OpenW /EXPORT:ExitFARW /EXPORT:ProcessSynchroEventW /EXPORT:PythonFar_AdvControl /EXPORT:PythonFar_PanelControl /EXPORT:PythonFar_DialogInit /EXPORT:PythonFar_DialogRun /EXPORT:PythonFar_DialogRunWithTimeout /EXPORT:PythonFar_DialogFree /EXPORT:PythonFar_SendDlgMessage /EXPORT:PythonFar_DefDlgProc /EXPORT:PythonFar_EditorControl /EXPORT:PythonFar_ViewerControl /EXPORT:Initialize=loader_Initialize /EXPORT:IsPlugin=loader_IsPlugin /EXPORT:CreateInstance=loader_CreateInstance /EXPORT:GetFunctionAddress=loader_GetFunctionAddress /EXPORT:GetError=loader_GetError /EXPORT:DestroyInstance=loader_DestroyInstance /EXPORT:Free=loader_Free"
)

# ===== Adapter DLL (PythonFar.adapter.dll) =====
add_library(PythonFar.adapter SHARED
    src/adapter/adapter.cpp
    src/adapter/interface.cpp
    src/adapter/dllmain.cpp
    src/adapter/plugin_functions.cpp
    src/adapter/bridges.cpp
    src/adapter/adapter.def
    src/logger.cpp
)
target_link_libraries(PythonFar.adapter ${Python3_LIBRARIES})
set_target_properties(PythonFar.adapter PROPERTIES
    PREFIX ""
    OUTPUT_NAME "PythonFar.adapter"
    LINK_FLAGS "/EXPORT:Initialize=adapter_Initialize /EXPORT:IsPlugin=adapter_IsPlugin /EXPORT:CreateInstance=adapter_CreateInstance /EXPORT:GetFunctionAddress=adapter_GetFunctionAddress /EXPORT:GetError=adapter_GetError /EXPORT:DestroyInstance=adapter_DestroyInstance /EXPORT:Free=adapter_Free"
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
    tests/test_loader.cpp
    tests/test_adapter.cpp
)
target_include_directories(pythonfar_tests PRIVATE src/loader src/adapter)
target_link_libraries(pythonfar_tests
    gtest_main
    ${Python3_LIBRARIES}
)

add_test(NAME PythonFarTests 
         COMMAND pythonfar_tests
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:pythonfar_tests>")

# ===== Struct Dumper =====
# struct_dumper emits compiler-truth struct offsets to JSON. It runs as a
# POST_BUILD step, so skip running it when cross-compiling (the produced binary
# targets a different architecture and cannot execute on the build host). The
# checked-in python/far_struct_offsets.json from a native build is used instead.
add_executable(struct_dumper tools/struct_dumper.cpp)
if(NOT PYTHONFAR_CROSS_BUILD)
    add_custom_command(TARGET struct_dumper POST_BUILD
        COMMAND struct_dumper "$<TARGET_FILE_DIR:PythonFar>/far_struct_offsets.json"
        COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE_DIR:PythonFar>/far_struct_offsets.json"
        "${CMAKE_CURRENT_SOURCE_DIR}/python/far_struct_offsets.json"
    )
endif()

# ===== Install =====
install(TARGETS PythonFar PythonFar.adapter
    RUNTIME DESTINATION .
    LIBRARY DESTINATION .
)
