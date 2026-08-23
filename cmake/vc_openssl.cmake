# Locate OpenSSL for the POSIX TLS layer (platform/posix/vc_tls_openssl.cpp).
#
# On macOS there is no system OpenSSL: /usr/bin/openssl is Apple's LibreSSL CLI
# and ships no headers or link libraries, and Homebrew keeps openssl@3 keg-only
# so it never lands on CMake's default search path. Probe the usual prefixes so
# a normal CLion "Reload CMake Project" works without hand-editing the cache.
#
# An explicit -DOPENSSL_ROOT_DIR=... always wins over everything below.

if(WIN32 OR OPENSSL_ROOT_DIR)
    return()
endif()

if(APPLE)
    set(_vc_openssl_hints "")

    # Whatever this Homebrew installation actually uses, arch included.
    find_program(VC_BREW_EXECUTABLE brew)
    if(VC_BREW_EXECUTABLE)
        execute_process(
            COMMAND ${VC_BREW_EXECUTABLE} --prefix openssl@3
            OUTPUT_VARIABLE _vc_brew_openssl
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(_vc_brew_openssl)
            list(APPEND _vc_openssl_hints "${_vc_brew_openssl}")
        endif()
    endif()

    list(APPEND _vc_openssl_hints
        /opt/homebrew/opt/openssl@3      # Homebrew, Apple silicon
        /usr/local/opt/openssl@3         # Homebrew, Intel
        /opt/local                       # MacPorts
        $ENV{HOME}/.local/openssl3       # local source build
    )

    foreach(_vc_hint IN LISTS _vc_openssl_hints)
        if(EXISTS "${_vc_hint}/include/openssl/ssl.h")
            set(OPENSSL_ROOT_DIR "${_vc_hint}" CACHE PATH "OpenSSL install prefix")
            message(STATUS "Using OpenSSL from ${_vc_hint}")
            break()
        endif()
    endforeach()

    if(NOT OPENSSL_ROOT_DIR)
        message(WARNING
            "No OpenSSL development files found. macOS ships none: /usr/bin/openssl "
            "is Apple's LibreSSL CLI with no headers. Install OpenSSL 3, e.g.\n"
            "  brew install openssl@3\n"
            "then reload CMake, or point the build at an existing install with\n"
            "  -DOPENSSL_ROOT_DIR=/path/to/openssl")
    endif()
endif()
