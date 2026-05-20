cmake_minimum_required(VERSION 3.16)

find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(archive-test test.cpp)
target_link_libraries(archive-test PRIVATE mo2::archive GTest::gtest GTest::gtest_main Qt::Core)

# copy test files
add_custom_command(TARGET archive-test POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/tests/files" "${CMAKE_CURRENT_BINARY_DIR}/files" COMMENT "Copy test files")

if(WIN32)
    # copy runtime DLLs
    add_custom_command(TARGET archive-test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_RUNTIME_DLLS:archive-test>
            $<TARGET_FILE_DIR:archive-test>
            COMMAND_EXPAND_LISTS
            COMMENT "Copy runtime DLLs for archive-test"
    )

    # copy 7zip.dll
    find_package(7zip CONFIG REQUIRED)
    add_custom_command(TARGET archive-test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_FILE:7zip::7zip>
            $<TARGET_FILE_DIR:archive-test>/dlls/7zip.dll
            COMMENT "Copy 7zip.dll"
    )
endif()

include(GoogleTest)
gtest_discover_tests(archive-test)
