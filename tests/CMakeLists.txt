enable_testing()

add_executable(xorbrute_tests
    test_xortool.cpp
)

target_link_libraries(xorbrute_tests PRIVATE
    xor_core
    Qt6::Core
    Qt6::Test
)

add_test(NAME xorbrute_tests COMMAND xorbrute_tests)

# Add custom target 'tests' requested by user
add_custom_target(tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    DEPENDS xorbrute_tests
)
