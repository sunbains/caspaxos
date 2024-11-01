cmake_minimum_required(VERSION 3.14)

project(caspaxos
        VERSION 1.0
	    DESCRIPTION "CASPaxos mesh network with Tests"
        LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

# Option for building tests
option(BUILD_TESTING "Build tests" ON)

# Find required packages
find_package(Threads REQUIRED)

# Create library target for the P2P mesh network
add_library(
    caspaxos
    src/caspaxos.cc
    src/net.cc
    include/net.h
    include/caspaxos.h
)

set_target_properties(caspaxos PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/lib")

# Set include directories for the library
target_include_directories(caspaxos
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link against required libraries
target_link_libraries(caspaxos PRIVATE Threads::Threads)

# Create installation rules
include(GNUInstallDirs)
install(TARGETS caspaxos
    EXPORT caspaxos-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    include/caspaxos.h
    include/net.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/caspaxos
)

# Export targets
install(EXPORT caspaxos-targets
    FILE caspaxos-targets.cmake
    NAMESPACE caspaxos::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caspaxos
)

# Testing configuration
if(BUILD_TESTING)
    enable_testing()
    
    # Include FetchContent for downloading GTest
    include(FetchContent)
    
    # Download and configure Google Test
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )
    
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    
    # Create test executable
    add_executable(net_tests tests/net_tests.cc)
    add_executable(caspaxos_tests tests/caspaxos_tests.cc)
    
    # Link against our library and GTest
    target_link_libraries(net_tests
        PRIVATE
            net
            GTest::gtest_main
            GTest::gmock_main
            caspaxos
    )

    target_link_libraries(caspaxos_tests
        PRIVATE
            caspaxos
            GTest::gtest_main
            GTest::gmock_main
            caspaxos
    )
    
    # Include test directory
    target_include_directories(net_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_include_directories(caspaxos_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/include)
    
    # Discover tests
    include(GoogleTest)
    gtest_discover_tests(net_tests)
    gtest_discover_tests(caspaxos_tests)

    # Add custom target for running tests with detailed output
    add_custom_target(test_verbose COMMAND ${CMAKE_CTEST_COMMAND} --verbose DEPENDS net_tests caspaxos_tests)
endif()

# Generate package config file
include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "caspaxos-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/caspaxos-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/caspaxos-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caspaxos
)

# Install package config files
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/caspaxos-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/caspaxos-config-version.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/caspaxos
)

