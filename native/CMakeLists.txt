cmake_minimum_required(VERSION 3.24)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../VERSION" INTERFAYCE_PROJECT_VERSION)
string(STRIP "${INTERFAYCE_PROJECT_VERSION}" INTERFAYCE_PROJECT_VERSION)
project(InterfayceOverlay VERSION ${INTERFAYCE_PROJECT_VERSION} LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "The first native Interfayce host targets Windows + SteamVR.")
endif()

include(FetchContent)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(INTERFAYCE_ICON "${CMAKE_CURRENT_SOURCE_DIR}/../assets/branding/interfayce.ico")
configure_file(src/interfayce.rc.in "${CMAKE_BINARY_DIR}/interfayce.rc" @ONLY)
FetchContent_Declare(
    openvr_sdk
    GIT_REPOSITORY https://github.com/ValveSoftware/openvr.git
    GIT_TAG v2.12.14
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR intentionally-empty
)
FetchContent_MakeAvailable(openvr_sdk)

add_executable(InterfaycePanelPreview EXCLUDE_FROM_ALL tests/panel_preview.cpp src/overlay_renderer.cpp)
add_executable(InterfayceAssistantLayoutTests tests/assistant_layout_tests.cpp)
target_include_directories(InterfayceAssistantLayoutTests PRIVATE src)
target_compile_features(InterfayceAssistantLayoutTests PRIVATE cxx_std_20)
target_compile_features(InterfaycePanelPreview PRIVATE cxx_std_20)
target_include_directories(InterfaycePanelPreview PRIVATE src "${openvr_sdk_SOURCE_DIR}/headers")
target_link_libraries(InterfaycePanelPreview PRIVATE d3d11 dxgi d2d1 dwrite windowscodecs ole32 shlwapi)

add_library(openvr_api SHARED IMPORTED GLOBAL)
set_target_properties(openvr_api PROPERTIES
    IMPORTED_IMPLIB "${openvr_sdk_SOURCE_DIR}/lib/win64/openvr_api.lib"
    IMPORTED_LOCATION "${openvr_sdk_SOURCE_DIR}/bin/win64/openvr_api.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${openvr_sdk_SOURCE_DIR}/headers"
)

add_executable(InterfayceOverlay
    "${CMAKE_BINARY_DIR}/interfayce.rc"
    src/main.cpp
    src/artwork_cache.cpp
    src/broadcast_controller.cpp
    src/desktop_capture.cpp
    src/desktop_surface_manager.cpp
    src/desktop_surface_registry.cpp
    src/overlay_renderer.cpp
    src/tray_icon.cpp
)
add_library(interfayce_battery_estimator STATIC
    src/battery_estimator.cpp
)
add_library(interfayce_private_windows STATIC src/private_window_manager.cpp)
target_compile_features(interfayce_private_windows PUBLIC cxx_std_20)
target_include_directories(interfayce_private_windows PUBLIC src)
target_link_libraries(interfayce_private_windows PUBLIC shell32 ole32 user32 advapi32 dwmapi setupapi)
add_executable(InterfayceWindowRecovery WIN32 src/window_recovery_main.cpp)
target_link_libraries(InterfayceWindowRecovery PRIVATE interfayce_private_windows)
add_executable(InterfaycePrivateWindowTests tests/private_window_tests.cpp)
target_include_directories(InterfaycePrivateWindowTests PRIVATE src)
target_compile_features(InterfaycePrivateWindowTests PRIVATE cxx_std_20)
target_link_libraries(InterfaycePrivateWindowTests PRIVATE shell32 ole32 user32 advapi32 dwmapi setupapi)
target_link_libraries(InterfayceOverlay PRIVATE interfayce_private_windows)
target_compile_features(interfayce_battery_estimator PRIVATE cxx_std_20)
target_include_directories(interfayce_battery_estimator PUBLIC src)
target_compile_features(InterfayceOverlay PRIVATE cxx_std_20)
target_compile_definitions(InterfayceOverlay PRIVATE
    INTERFAYCE_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(InterfayceOverlay PRIVATE interfayce_battery_estimator openvr_api d3d11 dxgi d2d1 dwrite dwmapi shell32 windowsapp windowscodecs ws2_32 crypt32 shlwapi)

add_executable(InterfayceBatteryEstimatorTests
    tests/battery_estimator_tests.cpp
)
add_executable(InterfayceDesktopCoordinatesTests tests/desktop_coordinates_tests.cpp)
target_include_directories(InterfayceDesktopCoordinatesTests PRIVATE src)
target_compile_features(InterfayceDesktopCoordinatesTests PRIVATE cxx_std_20)
target_link_libraries(InterfayceDesktopCoordinatesTests PRIVATE user32)
target_compile_features(InterfayceBatteryEstimatorTests PRIVATE cxx_std_20)
target_link_libraries(InterfayceBatteryEstimatorTests PRIVATE interfayce_battery_estimator)
# The installed overlay is a background VR utility, so a normal launch must not
# allocate a console window. Keep main/mainCRTStartup so the existing command
# probes and argument handling continue to work unchanged.
set_target_properties(InterfayceOverlay PROPERTIES WIN32_EXECUTABLE TRUE)
target_link_options(InterfayceOverlay PRIVATE /ENTRY:mainCRTStartup)

add_library(interfayce_process_audio STATIC
    src/process_loopback_capture.cpp
)
target_compile_features(interfayce_process_audio PRIVATE cxx_std_20)
target_link_libraries(interfayce_process_audio PUBLIC mmdevapi)

add_executable(InterfayceAudioEngine
    "${CMAKE_BINARY_DIR}/interfayce.rc"
    src/audio_engine_main.cpp
    src/audio_endpoint_renderer.cpp
)
target_compile_features(InterfayceAudioEngine PRIVATE cxx_std_20)
target_link_libraries(InterfayceAudioEngine PRIVATE interfayce_process_audio)

set(INTERFAYCE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(target InterfayceOverlay InterfayceAudioEngine InterfayceWindowRecovery InterfaycePrivateWindowTests)
    foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
        set_target_properties(${target} PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config}" "${INTERFAYCE_OUTPUT_DIRECTORY}"
        )
    endforeach()
endforeach()

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
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../assets/ui"
        "$<TARGET_FILE_DIR:InterfayceOverlay>/assets/ui"
)

add_executable(InterfayceArtworkCacheTests
    tests/artwork_cache_tests.cpp
    src/artwork_cache.cpp
)
target_include_directories(InterfayceArtworkCacheTests PRIVATE src)
target_compile_features(InterfayceArtworkCacheTests PRIVATE cxx_std_20)

add_executable(InterfayceDesktopClickTests tests/desktop_click_filter_tests.cpp)
target_include_directories(InterfayceDesktopClickTests PRIVATE src)
target_compile_features(InterfayceDesktopClickTests PRIVATE cxx_std_20)
