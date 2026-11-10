set(gtest_sources gtest_main.cc gtest/gtest-all.cc)
file(GLOB zoslib_test_sources "${CMAKE_CURRENT_SOURCE_DIR}/test-*.cc")

add_executable(cctest ${gtest_sources} ${zoslib_test_sources})
add_executable(cctest_a ${gtest_sources} ${zoslib_test_sources})
add_dependencies(cctest zoslib)
add_dependencies(cctest_a zoslib_a)

target_include_directories(cctest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cctest PRIVATE ${zoslib_defines})
target_compile_options(cctest PRIVATE ${zoslib_cflags})

target_include_directories(cctest_a PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(cctest_a PRIVATE ${zoslib_defines})
target_compile_options(cctest_a PRIVATE ${zoslib_cflags})

target_link_libraries(cctest_a zoslib_a)
target_link_libraries(cctest ${PROJECT_BINARY_DIR}/lib/libzoslib.x)

# Helper binary for the subprocess stdin-probe integration tests: it
# links zoslib so its process initialization (the untagged-stdin
# probe) runs before main(), with stdin redirected by the test.
add_executable(stdin_probe_helper stdin-probe-helper.cc)
target_include_directories(stdin_probe_helper PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/include)
# Compile like a real zopen consumer: ASCII mode AND bimodal, so the
# zoslib constructor's thread-mode switch establishes ThliCcsid=819
# before the stdin probe arms conversion (without bimodal, the
# SETCVTON program-ccsid defaults to ThliCcsid=1047 and the armed
# conversion becomes a no-op).
target_compile_options(stdin_probe_helper PRIVATE
    -m64 -march=z13 -mzos-target=zosv2r5 -fno-short-enums
    -fzos-le-char-mode=ascii -fgnu-keywords)
target_compile_definitions(stdin_probe_helper PRIVATE _AE_BIMODAL=1 _ALL_SOURCE)
target_link_libraries(stdin_probe_helper ${PROJECT_BINARY_DIR}/lib/libzoslib.x)

# Delivery driver: plain LE open (no zoslib), so the helper receives
# a clean untagged stdin.
add_executable(stdin_probe_driver stdin-probe-driver.c)
target_compile_options(stdin_probe_driver PRIVATE
    -m64 -mzos-target=zosv2r5 -fno-short-enums -fzos-le-char-mode=ascii)

target_compile_definitions(cctest PRIVATE
    TEST_HELPER_PATH="${CMAKE_CURRENT_BINARY_DIR}/stdin_probe_helper"
    TEST_DRIVER_PATH="${CMAKE_CURRENT_BINARY_DIR}/stdin_probe_driver")
