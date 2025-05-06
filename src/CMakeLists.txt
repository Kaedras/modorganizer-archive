cmake_minimum_required(VERSION 3.16)

add_library(archive STATIC)
mo2_configure_library(archive WARNINGS OFF PRIVATE_DEPENDS 7z bit7z)
target_compile_definitions(archive PRIVATE -DMODORGANIZER_ARCHIVE_BUILDING)

# embed bit7z in archive
target_sources(archive PRIVATE $<TARGET_OBJECTS:bit7z>)

mo2_install_target(archive)
