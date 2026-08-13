add_executable(vc-selftest
    src/main.cpp
)
target_link_libraries(vc-selftest PRIVATE vc_core)
vc_add_version_info(vc-selftest
    DESCRIPTION "VeriConnect Core Self-Test"
    KIND app)
