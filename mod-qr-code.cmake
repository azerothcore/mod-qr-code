#
# This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
#
# This file is executed during module configuration.
# Integrates mod-qr-code unit tests with AzerothCore's test framework.
#

if (BUILD_TESTING)
    message(STATUS "Configuring mod-qr-code tests...")

    file(GLOB_RECURSE MODULE_TEST_SOURCES
        "${CMAKE_SOURCE_DIR}/modules/mod-qr-code/tests/*.cpp"
    )

    if(MODULE_TEST_SOURCES)
        set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_SOURCES ${MODULE_TEST_SOURCES})

        # The encoder and renderer headers pull in no server types, so the tests only need
        # the module's own source directory on the include path.
        set_property(GLOBAL APPEND PROPERTY ACORE_MODULE_TEST_INCLUDES
            "${CMAKE_SOURCE_DIR}/modules/mod-qr-code/src"
            "${CMAKE_SOURCE_DIR}/modules/mod-qr-code/tests"
        )

        list(LENGTH MODULE_TEST_SOURCES TEST_FILE_COUNT)
        message(STATUS "  +- Registered ${TEST_FILE_COUNT} test file(s) from mod-qr-code")
    else()
        message(STATUS "  +- No test files found in mod-qr-code/tests")
    endif()
endif()
