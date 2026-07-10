cmake_minimum_required(VERSION 3.10)
project(miuchiz-usb VERSION 0.1.0 LANGUAGES C)

# Backend selection. macOS has no usable raw SCSI passthrough, so it must use
# libusb. On other platforms the native OS block-device backend is the default,
# but the libusb backend can be requested with -DMIUCHIZ_USE_LIBUSB=ON.
if(APPLE)
    set(_miuchiz_libusb_default ON)
else()
    set(_miuchiz_libusb_default OFF)
endif()
option(MIUCHIZ_USE_LIBUSB
       "Use the libusb backend instead of the native OS block device"
       ${_miuchiz_libusb_default})

if(APPLE AND NOT MIUCHIZ_USE_LIBUSB)
    message(WARNING "macOS requires the libusb backend; forcing MIUCHIZ_USE_LIBUSB=ON.")
    set(MIUCHIZ_USE_LIBUSB ON)
endif()

set(MIUCHIZ_USB_SOURCES
    src/backend-emu.c
    src/libmiuchiz-usb.c
    src/commands.c
    src/timer.c
    src/sleep.c
    src/log.c
    src/backend.c)

# The platform (real hardware) backend behind the backend.c dispatch layer.
# The emulator backend (backend-emu.c) is compiled on every platform.
if(MIUCHIZ_USE_LIBUSB)
    list(APPEND MIUCHIZ_USB_SOURCES src/backend-libusb.c)
elseif(WIN32)
    list(APPEND MIUCHIZ_USB_SOURCES src/backend-windows.c)
else()
    list(APPEND MIUCHIZ_USB_SOURCES src/backend-linux.c)
endif()

add_library(miuchiz-usb ${MIUCHIZ_USB_SOURCES})
set_property(TARGET miuchiz-usb PROPERTY C_STANDARD 11)

if(WIN32)
    # The emulator backend reaches emiu2 over loopback TCP on Windows.
    target_link_libraries(miuchiz-usb PRIVATE ws2_32)
endif()

# Conformance test for the shared Miuchiz Reborn path policy: runs the vector
# suite vendored from the miuchiz-reborn-paths repository against this
# library's C implementation. Run with ctest.
if(BUILD_TESTING)
    add_executable(paths-conformance tests/paths-conformance.c)
    target_link_libraries(paths-conformance PRIVATE miuchiz-usb)
    target_include_directories(paths-conformance PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    add_test(NAME paths-conformance
             COMMAND paths-conformance ${CMAKE_CURRENT_SOURCE_DIR}/tests/test-vectors.txt)
endif()

# PUBLIC so consumers (the miuchiz executable) inherit the headers, the libusb
# selection macro, and the libusb link/include flags automatically.
target_include_directories(miuchiz-usb PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

if(MIUCHIZ_USE_LIBUSB)
    target_compile_definitions(miuchiz-usb PUBLIC MIUCHIZ_USE_LIBUSB=1)

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(LIBUSB IMPORTED_TARGET libusb-1.0)
    endif()

    if(TARGET PkgConfig::LIBUSB)
        message(STATUS "miuchiz-usb: using libusb-1.0 ${LIBUSB_VERSION} via pkg-config")
        target_link_libraries(miuchiz-usb PUBLIC PkgConfig::LIBUSB)
    else()
        find_path(LIBUSB_INCLUDE_DIR
            NAMES libusb.h
            PATH_SUFFIXES libusb-1.0 libusb)
        find_library(LIBUSB_LIBRARY
            NAMES usb-1.0
            PATH_SUFFIXES lib lib32 lib64)
        if(NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
            message(FATAL_ERROR
                "libusb-1.0 not found. Install it (e.g. 'brew install libusb' on macOS, "
                "'apt install libusb-1.0-0-dev' on Debian/Ubuntu) or disable the libusb backend.")
        endif()
        message(STATUS "miuchiz-usb: using libusb at ${LIBUSB_LIBRARY}")
        target_include_directories(miuchiz-usb PUBLIC ${LIBUSB_INCLUDE_DIR})
        target_link_libraries(miuchiz-usb PUBLIC ${LIBUSB_LIBRARY})
    endif()
endif()
