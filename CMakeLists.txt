# SPDX-License-Identifier: GPL-2.0-or-later

cmake_minimum_required(VERSION 3.20)

project(obs-dock-layouts VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Widgets)
find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)

add_library(
  ${PROJECT_NAME}
  MODULE
  src/layout_dialog.cpp
  src/layout_dialog.hpp
  src/layout_store.cpp
  src/layout_store.hpp
  src/plugin.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE src)
target_link_libraries(
  ${PROJECT_NAME}
  PRIVATE
    OBS::libobs
    OBS::obs-frontend-api
    Qt6::Core
    Qt6::Widgets
)

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
    OUTPUT_NAME "obs-dock-layouts"
    PREFIX ""
)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra)
endif()

if(APPLE)
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version" FORCE)
  endif()
  set_target_properties(
    ${PROJECT_NAME}
    PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION plugin
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Info.plist.in"
  )

  file(
    GLOB_RECURSE plugin_data
    RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "${CMAKE_CURRENT_SOURCE_DIR}/data/*"
  )
  foreach(data_file IN LISTS plugin_data)
    get_filename_component(data_dir "${data_file}" DIRECTORY)
    if(data_dir STREQUAL "${data_file}")
      set(package_location "Resources")
    else()
      set(package_location "Resources/${data_dir}")
    endif()
    set_source_files_properties(
      "${CMAKE_CURRENT_SOURCE_DIR}/data/${data_file}"
      PROPERTIES MACOSX_PACKAGE_LOCATION "${package_location}"
    )
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/data/${data_file}")
  endforeach()

  install(TARGETS ${PROJECT_NAME} LIBRARY DESTINATION .)
else()
  install(
    TARGETS ${PROJECT_NAME}
    LIBRARY DESTINATION bin/64bit
    RUNTIME DESTINATION bin/64bit
  )
  install(DIRECTORY data/ DESTINATION data)
endif()

include(CTest)
if(BUILD_TESTING)
  add_executable(
    obs-dock-layouts-tests
    tests/layout_store_test.cpp
    src/layout_store.cpp
    src/layout_store.hpp
  )
  target_include_directories(obs-dock-layouts-tests PRIVATE src)
  target_link_libraries(obs-dock-layouts-tests PRIVATE Qt6::Core Qt6::Widgets)
  if(MSVC)
    target_compile_options(obs-dock-layouts-tests PRIVATE /W4 /utf-8 /permissive-)
  else()
    target_compile_options(obs-dock-layouts-tests PRIVATE -Wall -Wextra)
  endif()
  add_test(NAME layout_store COMMAND obs-dock-layouts-tests)
  set_tests_properties(layout_store PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
