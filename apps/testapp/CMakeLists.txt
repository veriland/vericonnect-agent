add_executable(vc-test
    src/main.c
)
target_link_libraries(vc-test PRIVATE vc_core)
vc_add_version_info(vc-test
    DESCRIPTION "VeriConnect Relay Test Tool"
    KIND app)
