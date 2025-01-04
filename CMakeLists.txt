cmake_minimum_required(VERSION 3.16)

include(CMakePackageConfigHelpers)

project(archive)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)
option(BUILD_SHARED_LIBS "Build using shared libraries" ON)

# configure bit7z options
# enable automatic format detection
set(BIT7Z_AUTO_FORMAT ON)
# Enables using the OS native string type (i.e., std::wstring on Windows, std::string elsewhere)
set(BIT7Z_USE_NATIVE_STRING ON)
if(UNIX)
    # enable support for older 7zip libraries, e.g. p7zip
    set(BIT7Z_USE_LEGACY_IUNKNOWN ON)
    if(BUILD_SHARED_LIBS)
        # enable position independent code, required to build as shared library
        # NOTE: target_compile_options won't work because there are other dependencies
        set(CMAKE_CXX_FLAGS " ${CMAKE_CXX_FLAGS} -fPIC")
        set(BIT7Z_GENERATE_PIC ON)
    endif()
endif()

add_subdirectory(src)

configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config.cmake"
  INSTALL_DESTINATION "lib/cmake/mo2-archive"
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/src/version.rc" archive_version)
string(REGEX MATCH "#define VER_FILEVERSION\\s*([0-9]+),([0-9]+),([0-9]+)" _ ${archive_version})
set(archive_version_major ${CMAKE_MATCH_1})
set(archive_version_minor ${CMAKE_MATCH_2})
set(archive_version_patch ${CMAKE_MATCH_3})

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config-version.cmake"
  VERSION "${archive_version_major}.${archive_version_minor}.${archive_version_patch}"
  COMPATIBILITY AnyNewerVersion
)

install(FILES
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/mo2-archive-config-version.cmake
  DESTINATION lib/cmake/mo2-archive
)
