cmake_minimum_required(VERSION 3.16)

add_library(archive)

find_package(Qt6 CONFIG REQUIRED COMPONENTS Core)

set_target_properties(archive PROPERTIES CXX_STANDARD 20)
target_link_libraries(archive PRIVATE bit7z Qt::Core)

target_sources(archive
	PRIVATE
		archive.cpp
		version.rc
	PUBLIC
		FILE_SET HEADERS
		BASE_DIRS ${CMAKE_CURRENT_LIST_DIR}/../include
		FILES ${CMAKE_CURRENT_LIST_DIR}/../include/archive/archive.h
)

target_include_directories(archive PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include/archive)

if (MSVC)
	target_compile_options(archive
		PRIVATE
		"/MP"
		"/W4"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(archive
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)

	set_target_properties(archive PROPERTIES VS_STARTUP_PROJECT archive)
else()
	target_compile_options(archive PRIVATE -Wall -Wextra -Wpedantic -fvisibility=hidden)
	target_link_options(archive PRIVATE  $<$<CONFIG:RelWithDebInfo>:-flto>)
endif()

# sets compiler threads to host core count, this is also applied to subprojects
# only set this variable if it does not exist to allow a user defined value
if (NOT CMAKE_BUILD_PARALLEL_LEVEL)
	include(ProcessorCount)
	ProcessorCount(HOST_PROC_COUNT)
	set(CMAKE_BUILD_PARALLEL_LEVEL ${HOST_PROC_COUNT})
	message(STATUS "Setting CMAKE_BUILD_PARALLEL_LEVEL to ${HOST_PROC_COUNT}")
endif ()

if (BUILD_STATIC)
	target_compile_definitions(archive PUBLIC -DMO2_ARCHIVE_BUILD_STATIC)
else()
	target_compile_definitions(archive PRIVATE -DMO2_ARCHIVE_BUILD_EXPORT)
endif()

add_library(mo2::archive ALIAS archive)

install(TARGETS archive EXPORT archiveTargets FILE_SET HEADERS)
if (NOT BUILD_STATIC AND WIN32)
	install(FILES $<TARGET_PDB_FILE:archive> DESTINATION pdb OPTIONAL)
endif()
install(EXPORT archiveTargets
	FILE mo2-archive-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-archive
)
