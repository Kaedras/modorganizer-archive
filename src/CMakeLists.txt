cmake_minimum_required(VERSION 3.16)

if(UNIX)
    # fix linker error
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
add_library(archive SHARED)
mo2_configure_library(archive WARNINGS OFF PRIVATE_DEPENDS 7z bit7z)
target_compile_definitions(archive PRIVATE -DMODORGANIZER_ARCHIVE_BUILDING)
mo2_install_target(archive)
