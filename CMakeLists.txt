# language: CMake, file: wisp-relay/CMakeLists.txt
cmake_minimum_required(VERSION 3.15)

project(wisp-relay C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# ------------------------------------------------------------------
# Platform detection
# ------------------------------------------------------------------
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    execute_process(
        COMMAND uname -o
        OUTPUT_VARIABLE WISP_RELAY_UNAME_O
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if(WISP_RELAY_UNAME_O MATCHES "Android")
        set(PLATFORM_TERMUX TRUE)
        message(STATUS "wisp-relay: targeting Termux/Android")
    else()
        message(STATUS "wisp-relay: targeting Linux/X11")
    endif()
endif()

# ------------------------------------------------------------------
# compat/
# ------------------------------------------------------------------
set(COMPAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/compat")
if(NOT EXISTS "${COMPAT_DIR}/posix.h")
    message(FATAL_ERROR "compat/posix.h not found at ${COMPAT_DIR}")
endif()
if(NOT EXISTS "${COMPAT_DIR}/threads.h")
    message(FATAL_ERROR "compat/threads.h not found at ${COMPAT_DIR}")
endif()

# ------------------------------------------------------------------
# WebView2 SDK (Windows only)
#
# Look for the SDK first at:
#   ${WEBVIEW2_SDK_DIR}/build/native/include/WebView2.h
#   ${WEBVIEW2_SDK_DIR}/build/native/x64/<loader>.lib
#
# Modern NuGet packages ship two loader libs in the arch directory:
#   WebView2LoaderStatic.lib  - self-contained, no DLL at runtime  <-- preferred
#   WebView2Loader.lib        - import stub, needs WebView2Loader.dll
# We pick the static one if it's there, and only fall back to the
# dynamic one (plus the POST_BUILD DLL copy) if it isn't.
#
# If neither the header nor a loader lib is found, download and extract
# the NuGet package, then re-check.
# ------------------------------------------------------------------
if(WIN32)
    set(WEBVIEW2_NUGET_VERSION "1.0.2739.15"
        CACHE STRING "Microsoft.Web.WebView2 NuGet package version")
    set(WEBVIEW2_SDK_DIR "${CMAKE_BINARY_DIR}/webview2-sdk"
        CACHE PATH "Root of the extracted Microsoft.Web.WebView2 NuGet package")

    set(WEBVIEW2_INCLUDE_DIR "${WEBVIEW2_SDK_DIR}/build/native/include")

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_wv2_arch x64)
    else()
        set(_wv2_arch x86)
    endif()
    set(_wv2_arch_dir "${WEBVIEW2_SDK_DIR}/build/native/${_wv2_arch}")

    # Function to (re)select the loader lib. Called after a possible
    # download, so the variable reflects whatever is on disk now.
    function(wisp_select_webview2_loader)
        set(_static "${_wv2_arch_dir}/WebView2LoaderStatic.lib")
        set(_shared "${_wv2_arch_dir}/WebView2Loader.lib")
        set(_dll    "${_wv2_arch_dir}/WebView2Loader.dll")

        if(EXISTS "${_static}")
            set(WEBVIEW2_LIB "${_static}" PARENT_SCOPE)
            set(WEBVIEW2_LIB_KIND "static" PARENT_SCOPE)
            set(WEBVIEW2_DLL "" PARENT_SCOPE)
        elseif(EXISTS "${_shared}")
            set(WEBVIEW2_LIB "${_shared}" PARENT_SCOPE)
            set(WEBVIEW2_LIB_KIND "dynamic" PARENT_SCOPE)
            if(EXISTS "${_dll}")
                set(WEBVIEW2_DLL "${_dll}" PARENT_SCOPE)
            else()
                set(WEBVIEW2_DLL "" PARENT_SCOPE)
            endif()
        else()
            set(WEBVIEW2_LIB "" PARENT_SCOPE)
            set(WEBVIEW2_LIB_KIND "" PARENT_SCOPE)
            set(WEBVIEW2_DLL "" PARENT_SCOPE)
        endif()
    endfunction()

    wisp_select_webview2_loader()

    if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h" OR NOT WEBVIEW2_LIB)
        message(STATUS "wisp-relay: WebView2 SDK not found at ${WEBVIEW2_SDK_DIR}")
        message(STATUS "wisp-relay: downloading ${WEBVIEW2_NUGET_VERSION} from nuget.org")

        set(_wv2_nupkg "${CMAKE_BINARY_DIR}/webview2.nupkg")
        file(DOWNLOAD
            "https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/${WEBVIEW2_NUGET_VERSION}"
            "${_wv2_nupkg}"
            STATUS wv2_dl_status
            TLS_VERIFY ON
            SHOW_PROGRESS)
        list(GET wv2_dl_status 0 wv2_dl_code)
        list(GET wv2_dl_status 1 wv2_dl_msg)
        if(NOT wv2_dl_code EQUAL 0)
            message(FATAL_ERROR
                "WebView2 SDK download failed: ${wv2_dl_msg}\n"
                "Download the .nupkg manually from\n"
                "  https://www.nuget.org/packages/Microsoft.Web.WebView2/${WEBVIEW2_NUGET_VERSION}\n"
                "rename it to .zip, extract it, and place the contents at\n"
                "  ${WEBVIEW2_SDK_DIR}\n"
                "so that\n"
                "  ${WEBVIEW2_SDK_DIR}/build/native/include/WebView2.h\n"
                "exists. Then reconfigure.")
        endif()

        file(REMOVE_RECURSE "${WEBVIEW2_SDK_DIR}")
        file(MAKE_DIRECTORY "${WEBVIEW2_SDK_DIR}")
        file(ARCHIVE_EXTRACT INPUT "${_wv2_nupkg}"
                                DESTINATION "${WEBVIEW2_SDK_DIR}")

        wisp_select_webview2_loader()
    else()
        message(STATUS "wisp-relay: WebView2 SDK found at ${WEBVIEW2_SDK_DIR}")
    endif()

    if(NOT EXISTS "${WEBVIEW2_INCLUDE_DIR}/WebView2.h")
        message(FATAL_ERROR
            "WebView2.h still missing after setup.\n"
            "  expected: ${WEBVIEW2_INCLUDE_DIR}/WebView2.h\n"
            "  SDK root: ${WEBVIEW2_SDK_DIR}\n"
            "Reconfigure with -DWEBVIEW2_SDK_DIR=<path> or extract the "
            "NuGet package manually so the path above exists.")
    endif()
    if(NOT WEBVIEW2_LIB)
        message(FATAL_ERROR
            "No WebView2 loader lib found.\n"
            "  looked in: ${_wv2_arch_dir}\n"
            "  wanted one of: WebView2LoaderStatic.lib, WebView2Loader.lib\n"
            "Reconfigure with -DWEBVIEW2_SDK_DIR=<path> or extract the "
            "NuGet package manually.")
    endif()

    message(STATUS "wisp-relay: WebView2 include:  ${WEBVIEW2_INCLUDE_DIR}")
    message(STATUS "wisp-relay: WebView2 loader:   ${WEBVIEW2_LIB} (${WEBVIEW2_LIB_KIND})")
    if(WEBVIEW2_DLL)
        message(STATUS "wisp-relay: WebView2 runtime:  ${WEBVIEW2_DLL}")
    endif()
endif()

# ------------------------------------------------------------------
# Backend selection
# ------------------------------------------------------------------
if(WIN32)
    set(RELAY_BACKEND "WEBVIEW_WEBVIEW2")
    set(PLATFORM_LIBS
        "${WEBVIEW2_LIB}"
        ole32 oleaut32 user32 gdi32 advapi32
        shell32 comctl32 uuid ws2_32
    )
    set(PLATFORM_INCLUDE_DIRS "${WEBVIEW2_INCLUDE_DIR}")

elseif(APPLE)
    set(RELAY_BACKEND "WEBVIEW_COCOA")
    find_library(COCOA_LIBRARY      Cocoa      REQUIRED)
    find_library(WEBKIT_LIBRARY     WebKit     REQUIRED)
    find_library(FOUNDATION_LIBRARY Foundation REQUIRED)
    set(PLATFORM_LIBS
        ${COCOA_LIBRARY} ${WEBKIT_LIBRARY} ${FOUNDATION_LIBRARY}
    )
    set(PLATFORM_INCLUDE_DIRS "")

elseif(UNIX)
    set(RELAY_BACKEND "WEBVIEW_GTK")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK REQUIRED gtk+-3.0)
    pkg_check_modules(WEBKIT webkit2gtk-4.1)
    if(NOT WEBKIT_FOUND)
        pkg_check_modules(WEBKIT REQUIRED webkit2gtk-4.0)
    endif()
    if(NOT PLATFORM_TERMUX)
        pkg_check_modules(X11 REQUIRED x11)
        set(X11_INCLUDE ${X11_INCLUDE_DIRS})
        set(X11_LIB     ${X11_LIBRARIES})
    endif()
    set(PLATFORM_LIBS
        ${GTK_LIBRARIES} ${WEBKIT_LIBRARIES} ${X11_LIB}
    )
    set(PLATFORM_INCLUDE_DIRS
        ${GTK_INCLUDE_DIRS} ${WEBKIT_INCLUDE_DIRS} ${X11_INCLUDE}
    )

else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

# ------------------------------------------------------------------
# Target
# ------------------------------------------------------------------
if(WIN32)
    add_executable(wisp-relay
        relay.c
        webview2.cpp
        mongoose.c
        cJSON.c
    )
else()
    add_executable(wisp-relay
        relay.c
        webview.c
        mongoose.c
        cJSON.c
    )
endif()

# ------------------------------------------------------------------
# MSVC: force the C files to compile as C.
# ------------------------------------------------------------------
if(WIN32 AND MSVC)
    set_source_files_properties(
        relay.c mongoose.c cJSON.c
        PROPERTIES
            LANGUAGE C
            COMPILE_FLAGS "/TC"
    )
    set_source_files_properties(
        webview2.cpp
        PROPERTIES
            LANGUAGE CXX
    )
    set_target_properties(wisp-relay PROPERTIES LINKER_LANGUAGE CXX)
endif()

target_compile_definitions(wisp-relay PRIVATE ${RELAY_BACKEND})

if(PLATFORM_TERMUX)
    target_compile_definitions(wisp-relay PRIVATE RELAY_TERMUX)
endif()

if(APPLE)
    set_source_files_properties(relay.c PROPERTIES
        COMPILE_OPTIONS "-x;objective-c"
    )
    target_compile_definitions(wisp-relay PRIVATE RELAY_MAC_OBJC)
endif()

target_include_directories(wisp-relay PRIVATE
    ${COMPAT_DIR}
    ${PLATFORM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(wisp-relay PRIVATE ${PLATFORM_LIBS})

if(NOT WIN32)
    find_package(Threads REQUIRED)
    target_link_libraries(wisp-relay PRIVATE Threads::Threads)
endif()

# ------------------------------------------------------------------
# Copy WebView2Loader.dll next to the exe, only if we linked the
# dynamic stub. The static lib needs no runtime DLL.
# ------------------------------------------------------------------
if(WIN32 AND WEBVIEW2_DLL)
    add_custom_command(TARGET wisp-relay POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${WEBVIEW2_DLL}"
            "$<TARGET_FILE_DIR:wisp-relay>/WebView2Loader.dll"
        COMMENT "Copying WebView2Loader.dll next to wisp-relay.exe"
        VERBATIM
    )
endif()

# ------------------------------------------------------------------
# Compile options
# ------------------------------------------------------------------
if(WIN32)
    if(MSVC)
        target_compile_options(wisp-relay PRIVATE
            /W3
            /wd4100
            /wd4996
            /utf-8
        )
        target_compile_definitions(wisp-relay PRIVATE
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
        )
    else()
        target_compile_options(wisp-relay PRIVATE -Wall -Wextra)
    endif()

    # Enable RC compilation on non-MSVC toolchains (MinGW, Clang, etc.).
    # MSVC enables it automatically.
    if(NOT MSVC)
        enable_language(RC)
    endif()

    target_sources(wisp-relay PRIVATE wisp-relay.rc)

elseif(APPLE)
    target_compile_options(wisp-relay PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations
    )

elseif(UNIX)
    target_compile_options(wisp-relay PRIVATE
        -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations
    )
endif()