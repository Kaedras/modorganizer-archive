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
if (NOT BUILD_STATIC)
	if (WIN32)
		install(FILES $<TARGET_PDB_FILE:archive> DESTINATION pdb OPTIONAL)
	else()
		# from https://stackoverflow.com/a/78987613
		# NOTE: The dbg extension is used solely because it is used as a standard
		# NOTE: In Linux documentation etc.
		# NOTE: Obtained from https://www.man7.org/linux/man-pages/man1/objcopy.1.html
		add_custom_command(
				TARGET archive
				# Separate the Debug Information from the Target file
				COMMAND ${CMAKE_OBJCOPY} ARGS --only-keep-debug $<TARGET_FILE:archive> $<TARGET_FILE:archive>.dbg
				# As we have previously separated the debug information from the
				# previous output, remove all debug information from the same
				COMMAND ${CMAKE_OBJCOPY} ARGS --strip-debug $<TARGET_FILE:archive>
				# Link the Separate Debug Information with the Final Release
				COMMAND ${CMAKE_OBJCOPY} ARGS --add-gnu-debuglink=$<TARGET_FILE:archive>.dbg $<TARGET_FILE:archive>
				POST_BUILD
		)
		# NOTE: Add this to the clean target
		# NOTE: This is needed because otherwise old debug information
		# NOTE:  Will not be cleared upon clean
		set_property(
				TARGET archive
				APPEND
				PROPERTY
				ADDITIONAL_CLEAN_FILES
				$<TARGET_FILE:archive>.dbg
		)
		install(FILES $<TARGET_FILE:archive>.dbg DESTINATION debug)
	endif()
endif()
install(EXPORT archiveTargets
	FILE mo2-archive-targets.cmake
	NAMESPACE mo2::
	DESTINATION lib/cmake/mo2-archive
)
