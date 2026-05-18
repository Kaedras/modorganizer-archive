cmake_minimum_required(VERSION 3.16)

find_package(GTest REQUIRED)

add_executable(archive-test test.cpp)
target_link_libraries(archive-test PRIVATE mo2::archive GTest::gtest_main)

add_custom_command(TARGET archive-test POST_BUILD COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/tests/files" "${CMAKE_CURRENT_BINARY_DIR}/files" COMMENT "Copy test files")

include(GoogleTest)
gtest_discover_tests(archive-test)
