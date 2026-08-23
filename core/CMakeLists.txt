# vc_core - portable core library.
# Everything under core/src is platform independent C11.
# Platform specific implementations (sockets, TLS, dynamic library
# loading, filesystem) live under platform/ and are selected here.

set(VC_CORE_SOURCES
    src/vc_common.cpp
    src/vc_fs.cpp
    src/vc_json.cpp
    src/vc_base64.cpp
    src/vc_sha256.cpp
    src/vc_url.cpp
    src/vc_ini.cpp
    src/vc_log.cpp
    src/vc_sas.cpp
    src/vc_ws.cpp
    src/vc_http.cpp
    src/vc_relay.cpp
    src/vc_adapter.cpp
    src/vc_settings.cpp
    src/vc_agent.cpp
)

if(WIN32)
    list(APPEND VC_CORE_SOURCES
        ../platform/win/vc_sock_win.cpp
        ../platform/win/vc_tls_schannel.cpp
        ../platform/win/vc_dynlib_win.cpp
        ../platform/win/vc_fs_win.cpp
        ../platform/win/vc_os_win.cpp
        ../platform/win/vc_impersonate_win.cpp
    )
else()
    list(APPEND VC_CORE_SOURCES
        ../platform/posix/vc_sock_posix.cpp
        ../platform/posix/vc_tls_openssl.cpp
        ../platform/posix/vc_dynlib_posix.cpp
        ../platform/posix/vc_fs_posix.cpp
        ../platform/posix/vc_os_posix.cpp
        ../platform/posix/vc_impersonate_posix.cpp
    )
endif()

add_library(vc_core STATIC ${VC_CORE_SOURCES})
target_include_directories(vc_core PUBLIC include)

if(WIN32)
    target_link_libraries(vc_core PUBLIC ws2_32 secur32 crypt32 advapi32)
else()
    # OpenSSL is required by the POSIX TLS layer (platform/posix/vc_tls_openssl.cpp).
    # cmake/vc_openssl.cmake, included from the top-level CMakeLists, has already
    # seeded OPENSSL_ROOT_DIR for the platforms that need the hint (macOS).
    find_package(OpenSSL REQUIRED)
    target_link_libraries(vc_core PUBLIC OpenSSL::SSL OpenSSL::Crypto pthread m)
    # libdl is a separate library on Linux but part of libSystem on macOS.
    if(NOT APPLE)
        target_link_libraries(vc_core PUBLIC dl)
    endif()
endif()
