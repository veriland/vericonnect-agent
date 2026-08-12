# vc_core - portable core library.
# Everything under core/src is platform independent C11.
# Platform specific implementations (sockets, TLS, dynamic library
# loading, filesystem) live under platform/ and are selected here.

set(VC_CORE_SOURCES
    src/vc_str.c
    src/vc_json.c
    src/vc_base64.c
    src/vc_sha256.c
    src/vc_url.c
    src/vc_ini.c
    src/vc_log.c
    src/vc_sas.c
    src/vc_ws.c
    src/vc_http.c
    src/vc_relay.c
    src/vc_adapter.c
    src/vc_settings.c
    src/vc_agent.c
)

if(WIN32)
    list(APPEND VC_CORE_SOURCES
        ../platform/win/vc_sock_win.c
        ../platform/win/vc_tls_schannel.c
        ../platform/win/vc_dynlib_win.c
        ../platform/win/vc_fs_win.c
        ../platform/win/vc_os_win.c
    )
else()
    list(APPEND VC_CORE_SOURCES
        ../platform/posix/vc_sock_posix.c
        ../platform/posix/vc_tls_openssl.c
        ../platform/posix/vc_dynlib_posix.c
        ../platform/posix/vc_fs_posix.c
        ../platform/posix/vc_os_posix.c
    )
endif()

add_library(vc_core STATIC ${VC_CORE_SOURCES})
target_include_directories(vc_core PUBLIC include)

if(WIN32)
    target_link_libraries(vc_core PUBLIC ws2_32 secur32 crypt32 advapi32)
else()
    target_link_libraries(vc_core PUBLIC ssl crypto dl pthread m)
endif()
