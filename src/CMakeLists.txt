cmake_minimum_required(VERSION 3.16)

find_package(7zip CONFIG REQUIRED)

add_library(archive)

if (UNIX)
	set(os_name "linux")
else ()
	set(os_name "win32")
endif ()

set_target_properties(archive PROPERTIES CXX_STANDARD 20)
target_link_libraries(archive PRIVATE 7zip::7zip)

target_sources(archive
	PRIVATE
		archive.cpp
		extractcallback.cpp
		extractcallback.h
		${os_name}/fileio.cpp
		${os_name}/fileio.h
		formatter.h
		inputstream.cpp
		inputstream.h
		instrument.h
		interfaceguids.cpp
		library.h
		multioutputstream.cpp
		multioutputstream.h
		opencallback.cpp
		opencallback.h
		propertyvariant.cpp
		propertyvariant.h
		unknown_impl.h
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
else ()
	target_compile_options(archive
		PRIVATE
		"-Wall"
		"-Wextra"
	)
	target_link_options(archive
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:-flto=auto>
	)
endif()

if (BUILD_STATIC)
	target_compile_definitions(archive PUBLIC -DMO2_ARCHIVE_BUILD_STATIC)
else()
	target_compile_definitions(archive PRIVATE -DMO2_ARCHIVE_BUILD_EXPORT)
endif()

add_library(mo2::archive ALIAS archive)

install(TARGETS archive EXPORT archiveTargets FILE_SET HEADERS)
if (WIN32 AND NOT BUILD_STATIC)
	install(FILES $<TARGET_PDB_FILE:archive> DESTINATION pdb OPTIONAL)
endif()
install(EXPORT archiveTargets
	FILE mo2-archive-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-archive
)
