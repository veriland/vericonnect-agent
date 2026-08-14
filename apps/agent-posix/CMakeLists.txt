# vc-agent - POSIX host (Linux/macOS). The Windows service host lives in
# apps/agent-win; both are thin wrappers around vc_agent_run() in core.
add_executable(vc-agent
        src/main.cpp
)
target_link_libraries(vc-agent PRIVATE vc_core)
