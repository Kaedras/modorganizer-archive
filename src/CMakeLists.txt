cmake_minimum_required(VERSION 3.16)

find_package(7zip CONFIG REQUIRED)

add_library(mo2-archive)

if (UNIX)
	set(os_name "linux")
else ()
	set(os_name "win32")
endif ()

set_target_properties(mo2-archive PROPERTIES CXX_STANDARD 20)
target_link_libraries(mo2-archive PRIVATE 7zip::7zip)

target_sources(mo2-archive
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

target_include_directories(mo2-archive PRIVATE ${CMAKE_CURRENT_LIST_DIR}/../include/archive)

if (MSVC)
	target_compile_options(mo2-archive
		PRIVATE
		"/MP"
		"/W4"
		"/external:anglebrackets"
		"/external:W0"
	)
	target_link_options(mo2-archive
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:/LTCG /INCREMENTAL:NO /OPT:REF /OPT:ICF>
	)

	set_target_properties(mo2-archive PROPERTIES VS_STARTUP_PROJECT archive)
else ()
	target_compile_options(mo2-archive
		PRIVATE
		"-Wall"
		"-Wextra"
		"-Wpedantic"
		"-fvisibility-ms-compat"
	)
	target_link_options(mo2-archive
		PRIVATE
		$<$<CONFIG:RelWithDebInfo>:-flto=auto>
	)
endif()

if (BUILD_STATIC)
	target_compile_definitions(mo2-archive PUBLIC -DMO2_ARCHIVE_BUILD_STATIC)
else()
	target_compile_definitions(mo2-archive PRIVATE -DMO2_ARCHIVE_BUILD_EXPORT)
endif()

add_library(mo2::archive ALIAS mo2-archive)
set_target_properties(mo2-archive PROPERTIES EXPORT_NAME archive)

install(TARGETS mo2-archive EXPORT archiveTargets FILE_SET HEADERS)
if (WIN32 AND NOT BUILD_STATIC)
	install(FILES $<TARGET_PDB_FILE:mo2-archive> DESTINATION pdb OPTIONAL)
endif()
install(EXPORT archiveTargets
	FILE mo2-archive-targets.cmake
	NAMESPACE mo2::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mo2-archive
)
