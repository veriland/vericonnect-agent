# The FileSystem adapter: a shared library implementing the adapter
# ABI (RunAdapterCommand / FreeAdapterString / GetAdapterInfo).
add_library(vc-adapter-filesystem SHARED
    src/fs_adapter.c
    src/fs_commands.c
)
target_link_libraries(vc-adapter-filesystem PRIVATE vc_core)

# Keep the vc-adapter- prefix used by the loader on every platform.
set_target_properties(vc-adapter-filesystem PROPERTIES
    PREFIX ""            # avoid an extra "lib" on Windows (already none)
    OUTPUT_NAME "vc-adapter-filesystem"
)
if(NOT WIN32)
    # produce libvc-adapter-filesystem.so / .dylib
    set_target_properties(vc-adapter-filesystem PROPERTIES PREFIX "lib")
endif()

vc_add_version_info(vc-adapter-filesystem
    DESCRIPTION "VeriConnect FileSystem Adapter"
    KIND dll)
