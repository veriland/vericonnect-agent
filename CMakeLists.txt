cmake_minimum_required(VERSION 3.20)
project(VeriConnect CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W3 /utf-8)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra)
endif()

# Put all runtime artifacts (exes, dlls) in one bin folder so the agent
# finds the adapters next to itself.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# ---------------------------------------------------------------------------
# Product / version metadata embedded into every Windows EXE and DLL.
# Bump these numbers here and every binary picks them up on rebuild.
# ---------------------------------------------------------------------------
set(VC_VER_MAJOR 1)
set(VC_VER_MINOR 0)
set(VC_VER_PATCH 0)
set(VC_VER_BUILD 0)
set(VC_VER_STR   "${VC_VER_MAJOR}.${VC_VER_MINOR}.${VC_VER_PATCH}.${VC_VER_BUILD}")
set(VC_COMPANY   "Veriland Consulting Ltd.")
set(VC_PRODUCT   "VeriConnect")
set(VC_COPYRIGHT "Copyright \\251 2026 Veriland Consulting Ltd.")

# vc_add_version_info(<target> DESCRIPTION "..." KIND app|dll)
# Generates a VERSIONINFO resource for the target so Explorer's Details
# tab (File description, Product name, Version, Copyright, ...) is filled.
# No-op on non-Windows builds.
function(vc_add_version_info target)
    if(NOT WIN32)
        return()
    endif()
    cmake_parse_arguments(ARG "" "DESCRIPTION;KIND" "" ${ARGN})

    get_target_property(out_name ${target} OUTPUT_NAME)
    if(NOT out_name)
        set(out_name ${target})
    endif()

    if(ARG_KIND STREQUAL "dll")
        set(VC_FILETYPE "0x2L")          # VFT_DLL
        set(VC_ORIG_NAME "${out_name}.dll")
    else()
        set(VC_FILETYPE "0x1L")          # VFT_APP
        set(VC_ORIG_NAME "${out_name}.exe")
    endif()
    set(VC_FILE_DESC     "${ARG_DESCRIPTION}")
    set(VC_INTERNAL_NAME "${out_name}")

    set(gen "${CMAKE_BINARY_DIR}/generated/${target}_version.rc")
    configure_file(${CMAKE_SOURCE_DIR}/cmake/version.rc.in ${gen} @ONLY)
    target_sources(${target} PRIVATE ${gen})
endfunction()

add_subdirectory(core)
add_subdirectory(adapters/filesystem)
add_subdirectory(apps/testapp)
add_subdirectory(apps/selftest)

if(WIN32)
    add_subdirectory(apps/agent-win)      # Windows service host
else()
    add_subdirectory(apps/agent-posix)    # Linux (systemd) / macOS (launchd) host
endif()

# Copy sample settings next to the binaries for convenience.
configure_file(${CMAKE_SOURCE_DIR}/config/Settings.ini
               ${CMAKE_BINARY_DIR}/bin/Settings.ini COPYONLY)
