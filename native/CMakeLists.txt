cmake_minimum_required(VERSION 3.24)

project(InterfayceOverlay LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "The first native Interfayce host targets Windows + SteamVR.")
endif()

include(FetchContent)
FetchContent_Declare(
    openvr_sdk
    GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
    GIT_TAG v2.12.14
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR intentionally-empty
)
FetchContent_MakeAvailable(openvr_sdk)

add_library(openvr_api SHARED IMPORTED GLOBAL)
set_target_properties(openvr_api PROPERTIES
    IMPORTED_IMPLIB "${openvr_sdk_SOURCE_DIR}/lib/win64/openvr_api.lib"
    IMPORTED_LOCATION "${openvr_sdk_SOURCE_DIR}/bin/win64/openvr_api.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${openvr_sdk_SOURCE_DIR}/headers"
)

add_executable(InterfayceOverlay
    src/main.cpp
    src/desktop_capture.cpp
    src/desktop_surface_manager.cpp
    src/desktop_surface_registry.cpp
    src/overlay_renderer.cpp
)
target_compile_features(InterfayceOverlay PRIVATE cxx_std_20)
target_link_libraries(InterfayceOverlay PRIVATE openvr_api d3d11 dxgi d2d1 dwrite dwmapi shell32 windowsapp windowscodecs ws2_32)

set(INTERFAYCE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(InterfayceOverlay PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config}" "${INTERFAYCE_OUTPUT_DIRECTORY}"
    )
endforeach()

set(INTERFAYCE_OVERLAY_EXE "${INTERFAYCE_OUTPUT_DIRECTORY}/InterfayceOverlay.exe")
configure_file(interfayce.vrmanifest.in interfayce.vrmanifest @ONLY)

add_custom_command(TARGET InterfayceOverlay POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/interfayce.vrmanifest"
        "$<TARGET_FILE_DIR:InterfayceOverlay>/interfayce.vrmanifest"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${openvr_sdk_SOURCE_DIR}/bin/win64/openvr_api.dll"
        "$<TARGET_FILE_DIR:InterfayceOverlay>/openvr_api.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/steamvr"
        "$<TARGET_FILE_DIR:InterfayceOverlay>/assets/steamvr"
)
