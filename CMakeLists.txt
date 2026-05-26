cmake_minimum_required(VERSION 3.16)

project(volchay-obs
    VERSION 0.1.0
    DESCRIPTION "Volchay Obs — modern C++ Twitch streaming client"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.2 COMPONENTS Widgets Network Multimedia REQUIRED)
# WebEngineWidgets powers the Browser source. Marked OPTIONAL_COMPONENTS
# so the project still configures on systems where qtwebengine isn't
# installed (e.g. -DVOLCHAY_NO_WEBENGINE=ON for headless builds); when
# missing the Browser source falls back to a placeholder render.
find_package(Qt6 6.2 COMPONENTS WebEngineWidgets QUIET)

# qt_standard_project_setup() is Qt 6.3+; emulate the bits we need so we
# stay buildable on Ubuntu 22.04's Qt 6.2.
if(COMMAND qt_standard_project_setup)
    qt_standard_project_setup()
endif()

set(VOLCHAY_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/PresetManager.cpp
    src/PresetManager.h
    src/StreamEngine.cpp
    src/StreamEngine.h
    src/ThemeManager.cpp
    src/ThemeManager.h
    src/PreviewWidget.cpp
    src/PreviewWidget.h
    src/AccentBadge.cpp
    src/AccentBadge.h
    src/Devices.cpp
    src/Devices.h
    src/Source.cpp
    src/Source.h
    src/SourcesPanel.cpp
    src/SourcesPanel.h
    src/SourceConfigDialog.cpp
    src/SourceConfigDialog.h
    src/AudioMixerPanel.cpp
    src/AudioMixerPanel.h
    src/AudioLevelMeter.cpp
    src/AudioLevelMeter.h
    src/AudioLevelProbe.cpp
    src/AudioLevelProbe.h
    src/WasapiLoopback.cpp
    src/WasapiLoopback.h
    src/I18n.cpp
    src/I18n.h
    src/Logger.cpp
    src/Logger.h
)

if(WIN32)
    list(APPEND VOLCHAY_SOURCES resources/app.rc)
endif()

qt_add_executable(volchay-obs WIN32 MACOSX_BUNDLE ${VOLCHAY_SOURCES})

qt_add_resources(volchay-obs "volchay_resources"
    PREFIX "/"
    FILES
        resources/themes/light.qss
        resources/themes/blackout.qss
        resources/themes/rgb.qss
        resources/icons/logo.svg
        resources/icons/logo.ico
        resources/icons/stream.svg
        resources/icons/settings.svg
        resources/icons/info.svg
        resources/icons/play.svg
        resources/icons/stop.svg
        resources/icons/mic.svg
        resources/icons/desktop.svg
        resources/icons/log.svg
        resources/icons/eye.svg
        resources/icons/eye-off.svg
)

target_include_directories(volchay-obs PRIVATE src)

target_link_libraries(volchay-obs PRIVATE
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)

if(TARGET Qt6::WebEngineWidgets)
    target_link_libraries(volchay-obs PRIVATE Qt6::WebEngineWidgets)
    target_compile_definitions(volchay-obs PRIVATE VOLCHAY_HAVE_WEBENGINE=1)
endif()

if(WIN32)
    set_target_properties(volchay-obs PROPERTIES
        WIN32_EXECUTABLE TRUE
        OUTPUT_NAME "VolchayObs"
    )
    # WASAPI loopback uses the Core Audio + COM stack. Both libraries
    # ship in the standard Windows SDK and don't bring in extra deps.
    target_link_libraries(volchay-obs PRIVATE ole32 uuid)
endif()

install(TARGETS volchay-obs
    BUNDLE  DESTINATION .
    RUNTIME DESTINATION bin
)
