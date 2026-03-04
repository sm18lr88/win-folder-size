cmake_minimum_required(VERSION 3.20)
project(foldersize LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Detours via vcpkg
# Works with both the vcpkg toolchain file (CI) and local vcpkg_installed/
list(APPEND CMAKE_PREFIX_PATH "${CMAKE_SOURCE_DIR}/vcpkg_installed/x64-windows")
find_path(DETOURS_INCLUDE_DIRS NAMES detours/detours.h REQUIRED)
find_library(DETOURS_LIBRARY NAMES detours REQUIRED)

# Use dynamic CRT (/MD) to match vcpkg x64-windows triplet
# Static CRT (/MT) causes heap corruption when mixing with vcpkg-built libs


# Source files
set(SOURCES
    src/dll_main.cpp
    src/core/logging.cpp
    src/core/size_formatter.cpp
    src/core/size_cache.cpp
    src/core/init.cpp
    src/core/size_resolver.cpp
    src/com/class_factory.cpp
    src/com/shell_overlay.cpp
    src/com/com_exports.cpp
    src/hooks/detour_wrapper.cpp
    src/hooks/hook_manager.cpp
    src/providers/everything_client.cpp
    src/providers/folder_scanner.cpp
)

# Version resource
set(RESOURCES src/version.rc)

# DLL target
add_library(foldersize SHARED ${SOURCES} ${RESOURCES})

# Include directories
target_include_directories(foldersize PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${DETOURS_INCLUDE_DIRS}
)

# Module definition file
set_target_properties(foldersize PROPERTIES
    LINK_FLAGS "/DEF:${CMAKE_SOURCE_DIR}/def/foldersize.def"
)

# System libraries
target_link_libraries(foldersize PRIVATE
    ole32 oleaut32 propsys shlwapi shell32 dbghelp advapi32 psapi
    ${DETOURS_LIBRARY}
)

# MSVC-specific flags
if(MSVC)
    target_compile_options(foldersize PRIVATE /W4 /WX /EHsc /FS)
    target_compile_definitions(foldersize PRIVATE UNICODE _UNICODE)
    
    # Debug: verbose logging
    target_compile_definitions(foldersize PRIVATE
        $<$<CONFIG:Debug>:FOLDERSIZE_DEBUG FS_LOG_LEVEL=0>
        $<$<CONFIG:Release>:FS_LOG_LEVEL=3>
    )
    
    # Release optimizations with debug info (PDB for crash debugging)
    target_compile_options(foldersize PRIVATE
        $<$<CONFIG:Release>:/O2 /GL /Zi>
    )
    
    # Release: Link-time code generation + PDB generation
    target_link_options(foldersize PRIVATE
        $<$<CONFIG:Release>:/LTCG /DEBUG /OPT:REF /OPT:ICF>
    )
endif()

# Tests
enable_testing()
find_package(GTest CONFIG REQUIRED)

add_executable(foldersize_tests
    tests/test_size_formatter.cpp
    tests/test_size_cache.cpp
    src/core/size_formatter.cpp
    src/core/size_cache.cpp
    src/core/logging.cpp
)

target_include_directories(foldersize_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(foldersize_tests PRIVATE GTest::gtest GTest::gtest_main)

target_compile_definitions(foldersize_tests PRIVATE UNICODE _UNICODE FS_LOG_LEVEL=5)

if(MSVC)
    target_compile_options(foldersize_tests PRIVATE /W4 /EHsc /FS)
endif()

# Copy GTest DLLs to build output dir (must match Release/Debug config)
add_custom_command(TARGET foldersize_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:GTest::gtest>
        $<TARGET_FILE_DIR:foldersize_tests>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:GTest::gtest_main>
        $<TARGET_FILE_DIR:foldersize_tests>
)

# Register test executable with CTest
add_test(NAME foldersize_tests COMMAND foldersize_tests)
