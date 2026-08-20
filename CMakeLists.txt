cmake_minimum_required(VERSION 3.16)

project(archive LANGUAGES CXX VERSION 2.1.3)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)
option(BUILD_TESTING "Build unit tests" OFF)

add_subdirectory(src)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/mo2-archive"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

message(STATUS "[MO2] Found version '${archive_VERSION}'.")

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config-version.cmake"
  VERSION "${archive_VERSION}"
  COMPATIBILITY AnyNewerVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config-version.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mo2-archive
)
