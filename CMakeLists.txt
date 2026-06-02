cmake_minimum_required(VERSION 3.14)
project(PythonFar)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Python
find_package(Python3 REQUIRED COMPONENTS Interpreter Development)

# Add include directories
include_directories(include)
include_directories(${Python3_INCLUDE_DIRS})

# ===== Loader Plugin (PythonFar.dll) =====
add_library(PythonFar SHARED
    src/loader/loader.cpp
    src/loader/interface.cpp
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
)
target_include_directories(pythonfar_tests PRIVATE src/loader)
target_link_libraries(pythonfar_tests
    gtest_main
)

add_test(NAME PythonFarTests 
         COMMAND pythonfar_tests
         WORKING_DIRECTORY "$<TARGET_FILE_DIR:pythonfar_tests>")

