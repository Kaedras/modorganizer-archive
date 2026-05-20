cmake_minimum_required(VERSION 3.16)

find_package(GTest REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Core)

add_executable(archive-test test.cpp)
target_link_libraries(archive-test PRIVATE mo2::archive GTest::gtest GTest::gtest_main Qt::Core)

add_custom_command(TARGET archive-test POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/tests/files" "${CMAKE_CURRENT_BINARY_DIR}/files" COMMENT "Copy test files")

if(WIN32)
    add_custom_command(TARGET archive-test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_RUNTIME_DLLS:archive-test>
            $<TARGET_FILE_DIR:archive-test>
            COMMAND_EXPAND_LISTS
            COMMENT "Copy runtime DLLs for archive-test"
    )
    add_custom_command(TARGET archive-test POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            $<TARGET_FILE_DIR:archive-test>/7zip.dll
            $<TARGET_FILE_DIR:archive-test>/dlls/7zip.dll
            COMMENT "Copy 7z.dll"
    )
endif()

include(GoogleTest)
gtest_discover_tests(archive-test)
