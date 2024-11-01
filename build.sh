#!/bin/bash
#
mkdir build && cd build

# Configure
cmake ..

# Build
cmake --build .

# Run tests
ctest

# Run tests with verbose output
cmake --build . --target test_verbose
