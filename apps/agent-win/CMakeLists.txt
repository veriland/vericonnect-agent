add_executable(vc-agent
    src/main.cpp
)
target_link_libraries(vc-agent PRIVATE vc_core)
vc_add_version_info(vc-agent
    DESCRIPTION "VeriConnect Agent Service"
    KIND app)
