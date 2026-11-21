cmake_minimum_required(VERSION 3.20)
project(gismo C CXX)

include(Grimace)

set(GISMO_DEVICE junosb CACHE STRING "Device profile: a folder in src/devices")

add_library(gismo_core STATIC
    src/core/draw.cpp
    src/core/filter.cpp
    src/core/grid.cpp
    src/core/inflate.cpp
    src/core/jpeg.cpp
    src/core/map_pack.cpp
    src/core/mission.cpp
    src/core/nmea.cpp
    src/core/places.cpp
    src/core/route.cpp
    src/core/session_log.cpp
    src/core/settings.cpp
    src/core/zip_writer.cpp)
target_include_directories(gismo_core PUBLIC src/core)
target_link_libraries(gismo_core PRIVATE grimace::headers)

add_library(gismo_platform STATIC
    src/platform/bitmap_file.cpp
    src/platform/gps_port.cpp
    src/platform/log.cpp)
target_include_directories(gismo_platform PUBLIC src/platform)
target_link_libraries(gismo_platform PUBLIC gismo_core PRIVATE grimace::headers)

grimace_add_executable(GISmo
    src/app/camera_screen.cpp
    src/app/canvas.cpp
    src/app/explore.cpp
    src/app/input.cpp
    src/app/main.cpp
    src/app/map_screen.cpp
    src/app/mission_screens.cpp
    src/app/outing.cpp
    src/app/photos.cpp
    src/app/session.cpp
    src/app/sky_screen.cpp
    src/app/storage.cpp
    src/app/ui.cpp
    src/devices/${GISMO_DEVICE}/device.cpp
    src/devices/${GISMO_DEVICE}/nullcam_camera.cpp
    ICON assets/gismo.ico)
target_include_directories(GISmo PRIVATE src/app)
target_link_libraries(GISmo PRIVATE gismo_platform grimace::aygshell)

grimace_add_executable(KeyTest src/diagnostics/keytest.cpp ICON assets/gismo.ico)
target_link_libraries(KeyTest PRIVATE grimace::aygshell)

grimace_add_executable(CamTest src/diagnostics/camtest.cpp ICON assets/gismo.ico)
target_link_libraries(CamTest PRIVATE gismo_platform grimace::aygshell)

grimace_package("GISmo" PROVIDER "Phil Howard"
    TARGETS GISmo
    FILES LICENSE THIRD-PARTY.md
    SHORTCUTS GISmo "GISmo")
